project('swayidled', 'c')
wireplumber = dependency('wireplumber-0.5')
executable('wp-swayidle',
  'main.c',
  dependencies: [wireplumber],
  install : true,
  install_dir : get_option('bindir')
)
