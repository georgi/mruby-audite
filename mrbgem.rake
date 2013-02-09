MRuby::Gem::Specification.new('mruby-audite') do |spec|
  spec.license = 'MIT'
  spec.authors = 'Matthias Georgi'
  spec.linker.libraries << 'portaudio'
  spec.linker.libraries << 'mpg123'
end
