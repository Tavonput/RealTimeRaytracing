import os
import subprocess

def find_vulkan_version() -> str:
    parentDirectory = "../Vendor/VulkanSDK"

    # There should only be one subdirectory
    subDirectories = [d for d in os.listdir(parentDirectory)]
    return subDirectories[0]

def check_shader_directory() -> None:
    bin_dir = "../Bin"
    shader_dir = "../Bin/Shaders"

    # Check for binaries directory
    if not os.path.exists(bin_dir):
        os.makedirs(bin_dir)
        print(f"Created directory {bin_dir}")

    # Check for shaders directory
    if not os.path.exists(shader_dir):
        os.makedirs(shader_dir)
        print(f"Created directory {shader_dir}")

def compile_shaders(vulkan_version) -> None:
    executable = f"../Vendor/VulkanSDK/{vulkan_version}/Bin/glslc.exe"

    # Vertex shader
    print("Compiling vertex shader")
    vert_src = "../RayTrace/Src/Shaders/shader.vert"
    vert_bin = "../Bin/Shaders/shader_vert.spv"
    subprocess.run([executable, vert_src, "-o", vert_bin])

    # Fragment shader
    print("Compiling fragment shader")
    frag_src = "../RayTrace/Src/Shaders/shader.frag"
    frag_bin = "../Bin/Shaders/shader_frag.spv"
    subprocess.run([executable, frag_src, "-o", frag_bin])

    print("Compilation finished")

if __name__ == "__main__":
    vulkan_version = find_vulkan_version()
    check_shader_directory()
    compile_shaders(vulkan_version)
