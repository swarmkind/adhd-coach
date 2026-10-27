#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')


def build(ctx):
    # The planner page is inlined into src/pkjs/planner_page.js before the JS is bundled.
    import subprocess
    subprocess.check_call(['node', 'tools/build-planner.js'], cwd=ctx.path.abspath())
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        # Link-time optimisation: about 3.5 KB off the app image, which must stay under 64 KB.
        # LTO keeps only what is referenced and cannot see the linker script's KEEP, so the app
        # header is named as referenced or it is dropped (check_header catches that).
        ctx.env.append_value('CFLAGS', ['-flto=auto'])
        ctx.env.append_value('LINKFLAGS', ['-flto=auto', '-Wl,--undefined=__pbl_app_info'])
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env
    ctx.add_post_fun(check_header)

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')


def check_header(ctx):
    # The watch reads the PebbleProcessInfo header from the start of the binary. Nothing in the
    # code refers to it, so a flag that lets the toolchain drop it still builds, into an app that
    # will not install.
    for platform in ctx.env.TARGET_PLATFORMS:
        path = os.path.join(ctx.bldnode.abspath(), ctx.all_envs[platform].BUILD_DIR, 'pebble-app.bin')
        with open(path, 'rb') as f:
            if f.read(6) != b'PBLAPP':
                ctx.fatal('{} does not start with the app header'.format(path))
