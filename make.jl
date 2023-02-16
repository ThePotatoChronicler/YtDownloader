import Downloads, TOML
using URIs: URIs, URI

# Make
include("juliamake.jl")
using .Make

# <CONSTANTS>
const BUILDFILE = "build.toml"
# </CONSTANTS>

function download_source(builddir, source; io::IO=stderr, taskctx)
    url = source["url"]
    uri = URI(url)
    filename = URIs.splitpath(uri) |> last
    filepath = "$builddir/$filename"

    newctx = Dict{Union{Symbol, String}, Any}()
    copy!(newctx, deepcopy(source))
    newctx[:filepath] = filepath

    if isfile(filepath)
        # TODO Hash checking
        return newctx
    end

    print(io, "Downloading $filename: ")
    majortotal = 10

    function progress(total, now)
        if total > 0
            ptotal = (now / total) * 100
            if ptotal > majortotal && majortotal < 100
                print("...$majortotal% ")
                flush(io)
                majortotal += 10
            end
        end
    end

    Downloads.download(url, filepath; progress)
    println("...100%")
    didwork(taskctx)

    return newctx
end

function download_sources(builddir, sources; taskctx = nothing)
    map(sources) do source
        download_source(builddir, source; taskctx)
    end
end

function create_symlinked_directory(realname::String, linkname; taskctx = nothing)::String
    realdir = "$(tempdir())/$realname"
    if !isdir(realdir)
        mkpath(realdir)
        didwork(taskctx)
    end

    if !islink(linkname)
        symlink(realdir, linkname; dir_target = true)
        didwork(taskctx)
    end

    return realdir
end

function unpack_source(file; taskctx)
    if isdir(file["out"])
        return
    end

    mkdir(file["out"])
    cd(file["out"]) do
        tarstrip = get(file, "strip", 0)
        run(`tar --auto-compress --extract --strip-components=$tarstrip --file $(file[:filepath])`)
    end
    didwork(taskctx)
end

function unpack_sources(builddir, files; taskctx = nothing)
    cd(builddir) do
        foreach(x -> unpack_source(x; taskctx), files)
    end
end

function main()
    buildfile = task("Read buildfile") do
        TOML.parsefile(BUILDFILE)
    end

    builddirlink::String = buildfile["base"]["build_directory_link"]
    appnamespace::String = buildfile["base"]["appns"]
    appname = split(appnamespace, ".") |> last

    builddirname = "$appnamespace.build"

    builddir = task("Create build directory") do taskctx
        create_symlinked_directory(builddirname, builddirlink; taskctx)
    end

    sources = buildfile["sources"]

    downloaded_sources = task("Download sources") do taskctx
        download_sources(builddir, sources; taskctx)
    end

    task("Unpack sources") do taskctx
        unpack_sources(builddir, downloaded_sources; taskctx)
    end

    task("Read out settings") do
        print("""
              # Buildfile settings
              App namespace: $appnamespace
              App name: $appname

              # Calculated settings
              Build directory name: $builddirname
              Build directory path: $builddir
              """)
    end
end

if !isinteractive()
    main()
end
