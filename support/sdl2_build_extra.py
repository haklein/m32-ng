Import("env", "projenv")

exec_name = "${BUILD_DIR}/${PROGNAME}${PROGSUFFIX}"

# Override unused "upload" target to execute the compiled simulator binary
from SCons.Script import AlwaysBuild
AlwaysBuild(env.Alias("upload", exec_name, exec_name))

# Also expose an explicit "execute" target in PlatformIO IDE
env.AddTarget(
    name="execute",
    dependencies=exec_name,
    actions='"{}"'.format(exec_name),
    title="Execute",
    description="Build and run the simulator",
    group="General",
)
