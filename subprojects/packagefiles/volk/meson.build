project('volk', 'c', version : '1.3.290', license : 'MIT')

volk_inc = include_directories('.')
volk_lib = static_library('volk',
  'volk.c',
  include_directories : volk_inc,
  install : false
)

volk_dep = declare_dependency(
  link_with : volk_lib,
  include_directories : volk_inc
)
