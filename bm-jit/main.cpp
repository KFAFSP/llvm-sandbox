#include <memory>
// std::unique_ptr
// std::make_unique
#include <iostream>
// std::cout
// std::cerr
#include <string>
// std::string
#include <sstream>
// std::ostringstream

#include "KaleidoscopeJIT.h"
// llvm::orc::KaleidoscopeJIT

#include "llvm/Support/TargetSelect.h"
// llvm::InitializeNativeTarget
// llvm::InitializeNativeTargetAsmPrinter
// llvm::InitializeNativeTargetAsmParser

#include "llvm/IR/LLVMContext.h"
// llvm::LLVMContext
#include "llvm/IR/DataLayout.h"
// llvm::DataLayout
#include "llvm/IR/Module.h"
// llvm::Module
#include "llvm/IR/IRBuilder.h"
// llvm::IRBuilder
#include "llvm/Analysis/InstructionSimplify.h"
// llvm::AnalysisManager<>
#include "llvm/IR/PassManager.h"
// llvm::PassManager<>
#include "llvm/Passes/PassBuilder.h"
// llvm::PassBuilder

#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

/** Function type declaration for string matchers.
 */
using MatchFunction = bool(const char*);

/** Abstraction class for managing generated functions.
 */
class CodeStorage {
public:
    /** Create a new \see CodeStorage instance.
     */
    CodeStorage() :
        _context(),
        _jit(),
        _builder(_context),
        _nextModule(0)
    { }

private:
    llvm::LLVMContext _context;
    llvm::orc::KaleidoscopeJIT _jit;
    llvm::IRBuilder<> _builder;

    unsigned _nextModule;

    std::unique_ptr<llvm::Module> createModule() {
        // Build a unique module name.
        std::ostringstream name;
        name << "module_" << _nextModule++;

        // Create the transient module instance.
        auto module = std::make_unique<llvm::Module>(name.str(), _context);
        // Set the module data layout.
        module->setDataLayout(_jit.getTargetMachine().createDataLayout());

        return module;
    }

    void optimizeFunction(std::shared_ptr<llvm::Module> module, llvm::Function &function) {
        llvm::PassBuilder pb;
        /*
        fpm = pb.buildFunctionSimplificationPipeline(
            llvm::PassBuilder::OptimizationLevel::O2,
            llvm::PassBuilder::ThinLTOPhase::None);
        */

        // Create a function pass manager.
        llvm::FunctionPassManager fpm{module.get()};

        // Add passes to the FPM.
        // Promote memory to register.
        fpm.addPass(llvm::PromotePass{});
        // Do simple "peephole" optimizations and bit-twiddling optzns.
        fpm.addPass(llvm::InstCombinePass{});
        // Reassociate expressions.
        fpm.addPass(llvm::ReassociatePass{});
        // Eliminate Common SubExpressions.
        fpm.addPass(llvm::GVN{});
        // Simplify the control flow graph (deleting unreachable blocks, etc).
        fpm.addPass(llvm::SimplifyCFGPass{});

        // Provide an AnalysisManager for the function pass.
        llvm::FunctionAnalysisManager fam{};
        // Add all analyses to the FAM (convenience).        
        pb.registerFunctionAnalyses(fam);
        
        // Run the optimization passes.
        fpm.run(function, fam);
    }

    void attachModule(std::unique_ptr<llvm::Module> module) {
        _jit.addModule(std::move(module));
    }
};

int main(int argc, char** argv)
{
    // Initialize LLVM for current machine target.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    

    return 0;
}