# PlatformIO pre-build step: use the Java app's dashboard.html as the single source of the dashboard page,
# copying it next to the firmware sources so it can be embedded in the binary.
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO)

project = env["PROJECT_DIR"]  # noqa: F821
source = os.path.join(project, "..", "src", "main", "resources", "dashboard.html")
target = os.path.join(project, "src", "web", "dashboard.html")

if os.path.exists(source):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if not os.path.exists(target) or open(source, "rb").read() != open(target, "rb").read():
        shutil.copyfile(source, target)
        print("dashboard.html copied from the Java app's resources")
elif not os.path.exists(target):
    raise SystemExit("dashboard.html not found: expected ../src/main/resources/dashboard.html")

# The same for the icon browsers ask for at /favicon.ico (Firefox ignores the page's data: icons in some cases).
icon_source = os.path.join(project, "..", "src", "main", "resources", "favicon.png")
icon_target = os.path.join(project, "src", "web", "favicon.png")
if os.path.exists(icon_source):
    if not os.path.exists(icon_target) or open(icon_source, "rb").read() != open(icon_target, "rb").read():
        shutil.copyfile(icon_source, icon_target)
        print("favicon.png copied from the Java app's resources")

# Touch main.cpp so it is recompiled on every build: the diagnostics show when this firmware was built (__DATE__ / __TIME__).
main_cpp = os.path.join(project, "src", "main.cpp")
if os.path.exists(main_cpp):
    os.utime(main_cpp, None)
