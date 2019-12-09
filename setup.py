from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import numpy as np

# Avoid a gcc warning below:
# cc1plus: warning: command line option ‘-Wstrict-prototypes’ is valid
# for C/ObjC but not for C++
class BuildExt(build_ext):
    def build_extensions(self):
        self.compiler.compiler_so.remove('-Wstrict-prototypes')
        super(BuildExt, self).build_extensions()

module = 'HWDecode'
        
hwdecode_utils_module = Extension(
        module,
		sources = ['hw_decode.cpp'],
		include_dirs=[np.get_include(), '/usr/local/include/'],
		extra_compile_args=['-DNDEBUG', '-O3'],
		extra_link_args=['-lavutil', '-lavcodec', '-lavformat', '-lswscale', '-L/usr/local/lib/']
)

setup ( name = module,
	version = '0.1',
	description = 'Utils for hwdecode',
	ext_modules = [ hwdecode_utils_module ],
	cmdclass = {'build_ext': BuildExt}
)
