from distutils.core import setup, Extension

setup(name='upb',
      version='0.1',
      ext_modules=[
          Extension('upb.cext', ['definition.c', 'pb.c', 'cext.c'],
              include_dirs=['../../src', '../../descriptor'],
              define_macros=[("UPB_USE_PTHREADS", 1),
                             ("UPB_UNALIGNED_READS_OK", 1),
                             ("UPB_THREAD_UNSAFE", 1)],
              library_dirs=['../../src'],
              libraries=['upb_pic'],
          ),
      ],
      packages=['upb']
      )
