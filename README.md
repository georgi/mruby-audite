Audite - a portable mp3 player for MRuby
========================================

Audite is a portable ruby library for playing mp3 files built on
libmp123 and portaudio.

## Features

* Nonblocking playback using native threads
* Realtime seeking
* Progress information
* Simple level meter

## Usage


```
p = Portaudio.new
m = Mpg123.new("01.mp3")
p.start(m)
p.seek(44100 * 100)
p.stop

```

## Requirements

* Mruby
* Portaudio >= 19
* Mpg123 >= 1.14

## Install

```
brew install portaudio
brew install mpg123
```
or
```
apt-get install libportaudiocpp0 portaudio19-dev libmpg123-dev
```

Add this line to `build_config.rb` in the mruby source folder:

```
conf.gem :git => 'git://github.com/georgi/mruby-audite.git', :branch => 'master'
```

## References

See also https://github.com/georgi/audite
