"""Exercise SDK invalidation and linker-script transfer without PlatformIO."""
from pathlib import Path
import runpy
import shutil
import tempfile

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/refresh_sdkconfig.py'


class Environment(dict):
    def __init__(self, root):
        super().__init__(BUILD_FLAGS=['-D PIN=1', '-Wall', '-D PIN=9'])
        self.root = root
        self.options = {'custom_sdkconfig': 'CONFIG_PM_ENABLE=y'}
        self.actions = []

    def subst(self, name):
        return {'$PROJECT_DIR': str(self.root), '$PIOENV': 'test',
                '$BUILD_DIR': str(self.root / 'build')}[name]

    def GetProjectOption(self, name, default):
        return self.options.get(name, default)

    def Replace(self, **values):
        self.update(values)

    def AddPostAction(self, target, action):
        self.actions.append(action)

    def PioPlatform(self):
        return self

    def get_package_dir(self, name):
        return str(self.root / 'libraries')

    def BoardConfig(self):
        return {'build.mcu': 'esp32c6'}


def apply(env):
    runpy.run_path(str(SCRIPT), init_globals={'env': env, 'Import': lambda _: None})


with tempfile.TemporaryDirectory(prefix='water-build-tests-') as temp:
    root = Path(temp)
    (root / 'scripts').mkdir()
    shutil.copy2(SCRIPT, root / 'scripts/refresh_sdkconfig.py')
    config = root / 'sdkconfig.test'
    config.write_text('old settings')
    env = Environment(root)
    apply(env)
    assert not config.exists()
    assert env['BUILD_FLAGS'] == ['-Wall', '-D PIN=9']
    config.write_text('resolved settings')
    apply(env)
    assert config.read_text() == 'resolved settings'
    env.options['custom_sdkconfig'] = 'CONFIG_PM_ENABLE=n'
    apply(env)
    assert not config.exists()

    destination = root / 'libraries/esp32c6/ld/sections.ld'
    destination.parent.mkdir(parents=True)
    destination.write_text('precompiled sections')
    env.actions[0](None, None, env)
    assert destination.read_text() == 'precompiled sections'
    (root / 'build').mkdir()
    (root / 'build/sections.ld').write_text('sections for rebuilt SDK')
    env.actions[0](None, None, env)
    assert destination.read_text() == 'sections for rebuilt SDK'

print('build_config_test: PASS')
