struct MethodMatchInfo
    applicable::Any
    ambig::Bool
end

struct UnionSplitInfo
    matches::Vector{MethodMatchInfo}
end

struct AbstractIterationInfo
    # The rt for each iterate call
    it_rt::Vector{Any}
    # The call info, for each implied call to `iterate` (in order)
    each::Vector{Any}
end

struct ApplyCallInfo
    # The info for the call itself
    call::Any
    # AbstractIterationInfo for each argument, if applicable
    arginfo::Vector{Union{Nothing, AbstractIterationInfo}}
end

struct UnionSplitApplyCallInfo
    infos::Vector{ApplyCallInfo}
end

struct CallMeta
    rt::Any
    info::Any
end
