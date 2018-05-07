#include "KaleidoscopeJIT.h"
#include "glob_pattern.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <iostream>
#include <string>

#define NUM_INPUT_STRINGS 100000000

static llvm::LLVMContext TheContext;
static llvm::IRBuilder<> Builder(TheContext);
static std::unique_ptr<llvm::Module> TheModule;
static llvm::Function *TheFunction;
static std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

static std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;

static llvm::AllocaInst *j_ptr;
static llvm::Value *str_ptr;

llvm::Value *generateLoadArrayElement(llvm::Value *arr, llvm::Value *ind) {
  return Builder.CreateLoad(Builder.CreateGEP(arr, ind));
}

llvm::Value *load_str_indptr(llvm::Value *ind_ptr) {
  return generateLoadArrayElement(str_ptr, Builder.CreateLoad(ind_ptr));
}

llvm::Value *load_str_j() { return load_str_indptr(j_ptr); }

void inc_ptr(llvm::Value *ptr) {
  Builder.CreateStore(
      Builder.CreateAdd(Builder.CreateLoad(ptr),
                        llvm::ConstantInt::get(TheContext, llvm::APInt(32, 1))),
      ptr);
}

llvm::Value *get_const_int(int bit_size, int val) {
  return llvm::ConstantInt::get(TheContext, llvm::APInt(bit_size, val));
}

void assign(llvm::Value *lvalue, llvm::Value *rvalue) {
  Builder.CreateStore(Builder.CreateLoad(rvalue), lvalue);
}

void generateFallback(llvm::BasicBlock *fallback) {
  if (fallback) {
    Builder.CreateBr(fallback);
  } else {
    Builder.CreateRet(get_const_int(1, 0));
  }
}

void generateMatchFunctionImpl(const std::string &pattern, int i,
                               llvm::BasicBlock *fallback) {

  // If it is the end of the pattern, we match if we've reached the end of the
  // string
  if (i == pattern.size()) {
    auto Cond = Builder.CreateICmpEQ(load_str_j(), get_const_int(8, 0));
    auto BB_true = llvm::BasicBlock::Create(TheContext, "", TheFunction);
    auto BB_false = llvm::BasicBlock::Create(TheContext, "", TheFunction);
    Builder.CreateCondBr(Cond, BB_true, BB_false);
    Builder.SetInsertPoint(BB_true);
    Builder.CreateRet(Cond);
    Builder.SetInsertPoint(BB_false);
    generateFallback(fallback);
    return;
  }

  // Shave a single char
  if (pattern[i] == '?') {
    auto Cond = Builder.CreateICmpNE(load_str_j(), get_const_int(8, 0));
    auto BB_true = llvm::BasicBlock::Create(TheContext, "", TheFunction);
    auto BB_false = llvm::BasicBlock::Create(TheContext, "", TheFunction);
    Builder.CreateCondBr(Cond, BB_true, BB_false);
    Builder.SetInsertPoint(BB_false);
    generateFallback(fallback);
    Builder.SetInsertPoint(BB_true);
    inc_ptr(j_ptr);
    generateMatchFunctionImpl(pattern, i + 1, fallback);
    return;
  }

  // Remove the wild card
  if (pattern[i] == '*') {
    auto k_ptr =
        Builder.CreateAlloca(llvm::Type::getInt32Ty(TheContext), 0, "k.ptr");
    assign(k_ptr, j_ptr);
    auto BB_loop = llvm::BasicBlock::Create(TheContext, "loop", TheFunction);
    auto BB_cond =
        llvm::BasicBlock::Create(TheContext, "loop.cond", TheFunction);
    auto BB_continue =
        llvm::BasicBlock::Create(TheContext, "loop.continue", TheFunction);
    auto BB_postloop =
        llvm::BasicBlock::Create(TheContext, "loop.post", TheFunction);
    Builder.CreateBr(BB_loop);
    Builder.SetInsertPoint(BB_loop);
    assign(j_ptr, k_ptr);
    generateMatchFunctionImpl(pattern, i + 1, BB_cond);
    Builder.SetInsertPoint(BB_cond);
    auto Cond =
        Builder.CreateICmpNE(load_str_indptr(k_ptr), get_const_int(8, 0));
    Builder.CreateCondBr(Cond, BB_continue, BB_postloop);
    Builder.SetInsertPoint(BB_continue);
    inc_ptr(k_ptr);
    Builder.CreateBr(BB_loop);
    Builder.SetInsertPoint(BB_postloop);
    generateFallback(fallback);

    return;
  }

  {
    auto Cond =
        Builder.CreateICmpEQ(load_str_j(), get_const_int(8, pattern[i]));
    auto BB_false = llvm::BasicBlock::Create(TheContext, "", TheFunction);
    auto BB_true = llvm::BasicBlock::Create(TheContext, "", TheFunction);
    Builder.CreateCondBr(Cond, BB_true, BB_false);
    Builder.SetInsertPoint(BB_false);
    generateFallback(fallback);
    Builder.SetInsertPoint(BB_true);
    inc_ptr(j_ptr);
    generateMatchFunctionImpl(pattern, i + 1, fallback);
    return;
  }
}

void generateMatchFunction(const std::string &pattern) {
  TheModule = std::make_unique<llvm::Module>("jit", TheContext);
  llvm::FunctionType *FT =
      llvm::FunctionType::get(llvm::Type::getInt1Ty(TheContext),
                              llvm::Type::getInt8PtrTy(TheContext), false);
  TheFunction = llvm::Function::Create(FT, llvm::Function::ExternalLinkage,
                                       "match", TheModule.get());
  llvm::BasicBlock *BB =
      llvm::BasicBlock::Create(TheContext, "entry", TheFunction);
  Builder.SetInsertPoint(BB);
  str_ptr = TheFunction->arg_begin();
  j_ptr = Builder.CreateAlloca(llvm::Type::getInt32Ty(TheContext), 0, "j.ptr");
  Builder.CreateStore(get_const_int(32, 0), j_ptr);
  generateMatchFunctionImpl(pattern, 0, nullptr);
}

double r() { return rand() * 1.0 / RAND_MAX; }

char rc() { return (char)('a' + r() * 26); }

std::string generate_string() {
  std::string res = "";
  if (r() < 0.5) {
    for (int i = 0, e = r() * 10 + 1; i < e; ++i) {
      res += rc();
    }
    return res;
  }
  if (r() < 0.8) {
    res += 'a';
  } else
    res += rc();

  for (int i = 0, e = r() * 5 + 1; i < e; ++i) {
    res += rc();
  }
  if (r() < 0.8) {
    res += 'b';
  } else
    res += rc();

  for (int i = 0, e = r() * 5 + 1; i < e; ++i) {
    res += rc();
  }
  if (r() < 0.8) {
    res += 'c';
  } else
    res += rc();

  res += rc();
  return res;
}

template <typename TimeT = std::chrono::duration<double>> struct measure {
  template <typename F, typename... Args>
  static auto duration(F &&func, Args &&... args) {
    auto start = std::chrono::high_resolution_clock::now();
    std::forward<decltype(func)>(func)(std::forward<Args>(args)...);
    return std::chrono::duration_cast<TimeT>(
        std::chrono::high_resolution_clock::now() - start);
  }
};

int main() {
  srand(time(nullptr));
  bool (*FP)(const char *);

  std::cout << "Initialize JIT engine: "
            << measure<>::duration([&]() {

                 llvm::InitializeNativeTarget();
                 llvm::InitializeNativeTargetAsmPrinter();
                 llvm::InitializeNativeTargetAsmParser();
                 TheJIT = std::make_unique<llvm::orc::KaleidoscopeJIT>();

               })
                   .count()
            << std::endl;

  std::cout << "JIT-compile of match function: "
            << measure<>::duration([&]() {

                 generateMatchFunction("a*b*c?");

                 TheModule->setDataLayout(
                     TheJIT->getTargetMachine().createDataLayout());
                 // Create a new pass manager attached to it.
                 TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(
                     TheModule.get());

                 // Promote allocas to registers.
                 TheFPM->add(llvm::createPromoteMemoryToRegisterPass());
                 // Do simple "peephole" optimizations and bit-twiddling optzns.
                 TheFPM->add(llvm::createInstructionCombiningPass());
                 // Reassociate expressions.
                 TheFPM->add(llvm::createReassociatePass());
                 // Eliminate Common SubExpressions.
                 TheFPM->add(llvm::createGVNPass());
                 // Simplify the control flow graph (deleting unreachable
                 // blocks, etc).
                 TheFPM->add(llvm::createCFGSimplificationPass());

                 TheFPM->doInitialization();

                 TheFPM->run(*TheFunction);
                 // Print out all of the generated code.
                 // TheModule->print(llvm::errs(), nullptr);

                 auto H = TheJIT->addModule(std::move(TheModule));

                 auto ExprSymbol = TheJIT->findSymbol("match");
                 assert(ExprSymbol && "Function not found");
                 FP = (bool (*)(const char *))(intptr_t)cantFail(
                     ExprSymbol.getAddress());
               })
                   .count()
            << "s" << std::endl;

  // Generate vector
  std::vector<std::string> inputs;
  inputs.reserve(NUM_INPUT_STRINGS);

  std::cout << "Generating input strings (" << NUM_INPUT_STRINGS << "): "
            << measure<>::duration([&]() {

                 for (int i = 0; i < NUM_INPUT_STRINGS; ++i) {
                   inputs.push_back(generate_string());
                 }
               })
                   .count()
            << "s" << std::endl;

  if (0) {
    for (int i = 0; i < inputs.size(); ++i) {
      if (matchGeneric("a*b*c?", inputs[i].c_str()) != FP(inputs[i].c_str())) {
        std::cout << "Mismatch: " << inputs[i]
                  << ", Generic: " << matchGeneric("a*b*c?", inputs[i].c_str())
                  << ", Specialized: " << FP(inputs[i].c_str()) << std::endl;
      }
    }
  }

  int gn = 0, gfn = 0, jn = 0;

  std::cout << "Generic match: "
            << measure<>::duration([&]() {

                 for (int i = 0; i < inputs.size(); ++i) {
                   gn += matchGeneric("a*b*c?", inputs[i].c_str());
                 }
               })
                   .count()
            << "s" << std::endl;

  std::cout << "Generic match function with a fixed pattern: "
            << measure<>::duration([&]() {

                 for (int i = 0; i < inputs.size(); ++i) {
                   gfn += matchFixed(inputs[i].c_str());
                 }
               })
                   .count()
            << "s" << std::endl;
  std::cout << "JIT-compiled match: "
            << measure<>::duration([&]() {

                 for (int i = 0; i < inputs.size(); ++i) {
                   jn += FP(inputs[i].c_str());
                 }
               })
                   .count()
            << "s" << std::endl;

  if (gn != jn) {
    std::cout << "Error" << std::endl;
  }
  return 0;
}
