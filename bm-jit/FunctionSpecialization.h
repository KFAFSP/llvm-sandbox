#include <limits>
// std::numeric_limits<>
#include <vector>
// std::vector<>
#include <type_traits>
// std::is_move_constructible

#include "llvm/ExecutionEngine/JITSymbol.h"
// llvm::JITSymbol

/** Dummy class for empty specialization bindings. */
class Binding {
public:
    /** Type used for quantifying the match between two Binding instances. */
    using match_t = int;
    /** Indicates a total mismatch. */
    static constexpr match_t mismatch = std::numeric_limits<match_t>::min();
    /** Indicates a perfect match. */
    static constexpr match_t match = std::numeric_limits<match_t>::max();

public:
    // Ensure that there is a default constructor.
    Binding() = default;
    // Ensure that there is a move constructor.
    Binding(Binding&&) = default;

    /** Match this binding to a requested binding.
     * 
     * \retval  mismatch    Indicates that this binding is completely incompatible with the requested one.
     * \retval  <>          Gives a score for this binding that is comparable to other invocations of this method.
     * \retval  match       Indicates that this binding is a perfect match for the requested one.
     */
    int matchWith(const Binding&) {
        // By default, let all functions be inequal.
        return mismatch;
    }
};

/** Class for storing JIT-compiled function specializations. */
template<
    // This is supposed to be the function type of the specialized function,
    // e.g. bool(const char*).
    typename FunctionType,
    // This is supposed to be the type that stores the runtime parameters
    // for the function that can be specialized.
    typename RuntimeParams = Binding
>
class FunctionSpecialization : public RuntimeParams {
    static_assert(std::is_move_constructible<RuntimeParams>::value, "RuntimeParams must be move-constructible.");
    // TODO : Insert static_asserts that verify RuntimeParams more.

public:
    /** Type alias for the function type. */
    using func_type     = FunctionType;
    /** Type alias for the function pointer type. */
    using func_pointer  = FunctionType*;

public:
    /** Create a new function specialization.
     * 
     * \param   [in,out]    jitSymbol       JIT Symbol that implements the function (will be moved).
     * \param   [in,out]    runtimeParams   Binding of runtime parameters that describe the specialization.
     */
    FunctionSpecialization(
        llvm::JITSymbol&& jitSymbol,
        RuntimeParams&& runtimeParams) :
    RuntimeParams(std::move(runtimeParams)),
    _jitSymbol(std::move(jitSymbol)),
    _pointer(nullptr)
    { }

    // No copy constructor.
    FunctionSpecialization(const FunctionSpecialization&) = delete;

    /** Move a function specialization.
     * 
     * \param   [in,out]    move    Instance to move.
     */
    FunctionSpecialization(FunctionSpecialization&& move) :
    RuntimeParams(std::move(move)),
    _jitSymbol(std::move(move._jitSymbol)),
    _pointer(move._pointer)
    {
        move._pointer = nullptr;
    }

    /** Get the pointer to the function.
     * 
     * \return  Pointer to the function.
     */
    func_pointer get_pointer()
    {
        if (!_pointer) {
            // Get the address of the symbol and cast it to the pointer type.
            const auto addr = static_cast<intptr_t>(llvm::cantFail(_jitSymbol.getAddress()));
            _pointer = reinterpret_cast<func_pointer>(addr);
        }

        return _pointer;
    }

    /** Invoke the specialized function by perfect forwarding. */
    template<typename ...Args>
    auto invoke(Args && ...args)
    {
        return get_pointer()(std::forward(args)...);
    }

private:
    llvm::JITSymbol _jitSymbol;
    func_pointer    _pointer;
};

template<typename FunctionType, typename RuntimeParams>
class SpecializationStorage {
public:
    /** Type alias for the specialization type. */
    using Specialization = FunctionSpecialization<FunctionType, RuntimeParams>;

    /** Enumeration of possible find() strategies. */
    enum class FindStrategy {
        /** Find the first matching specialization. */
        First,
        /** Find the last matching specialization. */
        Last,
        /** Find the specialization with the highest score. */
        Best
    };

public:
    // Ensure that there is a default constructor.
    SpecializationStorage() = default;
    // Ensure that there is a move constructor.
    SpecializationStorage(SpecializationStorage&&) = default;

    /** Store a new specialization in the container.
     * 
     * \param   [in,out]    jitSymbol       JIT Symbol that implements the function (will be moved).
     * \param   [in,out]    runtimeParams   Binding of runtime parameters that describe the specialization.
     * 
     * \return  Reference to created Specialization.
     */
    const Specialization& store(
        llvm::JITSymbol&& jitSymbol,
        RuntimeParams&& runtimeParams)
    {
        _storage.emplace_back(jitSymbol, runtimeParams);
        return _storage.back();
    }
    /** Evict a Specialization from the container.
     * 
     * \param   [in]    specialization  Specialization to evict.
     * 
     * \retval  true    Specialization was found and evicted.
     * \retval  false   Specialization was not found.
     */
    bool evict(const Specialization& specialization)
    {
        for (const auto it = _storage.cbegin(); it != _storage.cend(); ++it) {
            if (it == &specialization) {
                _storage.erase(it);
                return true;
            }
        }

        return false;
    }

    /** Find an appropriate specialization for the requested binding.
     * 
     * \param   [in]    binding     Binding to compare with
     * \param   [out]   score       Score of the matched binding.
     * \param   [in]    strategy    Strategy to use.
     * 
     * \retval  nullptr No match found.
     * \return  Constant pointer to matched specialization.
     */
    const Specialization* find(const RuntimeParams& binding, Binding::match_t& score, FindStrategy strategy)
    {
        switch (strategy) {
            case FindStrategy::First:
                return find_first(binding, score);

            case FindStrategy::Last:
                return find_last(binding, score);

            case FindStrategy::Best:
                return find_best(binding, score);
        }
    }

private:
    const Specialization* find_first(const RuntimeParams& binding, Binding::match_t& score)
    {
        // Traverse from the front.
        for (const auto it = _storage.cbegin(); it != _storage.cend(); ++it) {
            score = it->matchWith(binding);
            if (score != Binding::mismatch) {
                return it;
            }
        }

        // If _storage was empty, score is undefined.
        score = Binding::mismatch;
        return nullptr;
    }
    const Specialization* find_last(const RuntimeParams& binding, Binding::match_t& score)
    {
        // Traverse from the back.
        for (const auto it = _storage.crbegin(); it != _storage.crend(); ++it) {
            score = it->matchWith(binding);
            if (score != Binding::mismatch) {
                return it;
            }
        }

        // If _storage was empty, score is undefined.
        score = Binding::mismatch;
        return nullptr;
    }
    const Specialization* find_best(const RuntimeParams& binding, Binding::match_t& score)
    {
        // Initialize worst score and no match.
        score = Binding::mismatch;
        const Specialization* best = nullptr;
    
        // Traverse from the front.
        for (const auto it = _storage.cbegin(); it != _storage.cend(); ++it) {
            auto temp = it->matchWith(binding);

            switch (temp) {
                case Binding::match:
                    // Perfect match encountered.
                    score = temp;
                    return it;

                default:
                    if (temp > score) {
                        // Update match winner.
                        best = it;
                        score = temp;
                    }
            }
        }

        return best;
    }

    std::vector<Specialization> _storage;
};