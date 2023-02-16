module Make

export task, TaskContext, didwork

get_task_name(f::Function) = nameof(f) |> String
get_task_name(f::String) = f

mutable struct TaskContext
    const taskname::String
    did_work::Bool
end

# Default to no work done
TaskContext(t) = TaskContext(t, false)

# Ignore nothing, for ease of use
didwork(::Nothing) = nothing
didwork(t::TaskContext) = t.did_work = true;

function task(f::Function, taskname; io::IO=stderr)
    name = get_task_name(taskname)
    ctx = TaskContext(name)
    print(io, "> Starting task ")
    printstyled(io, "$name\n", color=:green)

    applied = applicable(f, ctx)
    val = if applied
        f(ctx)
    else
        f()
    end

    if applied && !ctx.did_work
        println("| Nothing to do")
    end

    print(io, "> Exitting task ")
    printstyled(io, "$name\n", color=:light_green)
    return val
end

end
