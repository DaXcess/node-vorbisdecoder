{
  'targets': [{
    'target_name': 'vorbis-dec',
    'include_dirs': [ "<!(node -e \"require('nan')\")" ],
    'sources': [
      'src/binding.cc'
    ],
    'libraries': ['-lvorbis', '-logg', '-lm']
  }]
}