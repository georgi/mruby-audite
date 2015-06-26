#include <mruby.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/array.h>

#include <pthread.h>
#include <stdio.h>
#include <math.h>
#include <mpg123.h>
#include <portaudio.h>

typedef struct {
  PaStream *stream;
  mpg123_handle *mpg123;
  int position;
  float rms;
} Portaudio;

static struct RClass *mrb_portaudio_class;
static struct RClass *mrb_mpg123_class;

float rms(float *v, int n)
{
  int i;
  float sum = 0.0;

  for (i = 0; i < n; i++) {
    sum += v[i] * v[i];
  }

  return sqrt(sum / n);
}

void mrb_mpg123_free(mrb_state *mrb, void *mh)
{
  mpg123_close(mh);
  mpg123_delete(mh);
}

void mrb_portaudio_free(mrb_state *mrb, void *ptr)
{
  Portaudio *portaudio = (Portaudio *) ptr;

  if (portaudio) {
    Pa_CloseStream(portaudio->stream);
    free(portaudio);
  }
}

static struct mrb_data_type mrb_mpg123_data = {
  .struct_name = "Mpg123",
  .dfree = mrb_mpg123_free
};

static struct mrb_data_type mrb_portaudio_data = {
  .struct_name = "Portaudio",
  .dfree = mrb_portaudio_free
};

mrb_value mrb_mpg123_new(mrb_state *mrb, mrb_value klass) {
  int err = MPG123_OK;
  mpg123_handle *mh;
  mrb_value filename;
  long rate;
  int channels, encoding;

  mrb_get_args(mrb, "s", &filename);

  if ((mh = mpg123_new(NULL, &err)) == NULL) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mpg123_plain_strerror(err));
  }

  mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);

  if (mpg123_open(mh, (char*) mrb_str_ptr(filename)) != MPG123_OK ||
      mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mpg123_strerror(mh));
  }

  if (encoding != MPG123_ENC_FLOAT_32) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "bad encoding");
  }

  mpg123_format_none(mh);
  mpg123_format(mh, rate, channels, encoding);

  return mrb_obj_value(mrb_data_object_alloc(mrb, mrb_mpg123_class, mh, &mrb_mpg123_data));
}

mrb_value mrb_mpg123_close(mrb_state *mrb, mrb_value self)
{
  mpg123_close(DATA_PTR(self));
  return self;
}


mrb_value mrb_mpg123_read(mrb_state *mrb, mrb_value self)
{
  mrb_value _size;
  mrb_get_args(mrb, "i", &_size);
  int size = mrb_fixnum(_size);
  float *buffer = malloc(size * sizeof(float));
  mrb_value result = mrb_ary_new_capa(mrb, size);
  mpg123_handle *mh = NULL;
  size_t done = 0;
  int i;
  int err = MPG123_OK;

  mh = mrb_check_datatype(mrb, self, &mrb_mpg123_data);
  err = mpg123_read(mh, (unsigned char *) buffer, size * sizeof(float), &done);

  if (err == MPG123_OK || err == MPG123_DONE) {
    for (i = 0; i < size; i++) {
      mrb_ary_set(mrb, result, i, mrb_float_value(mrb, buffer[i]));
    }
    free(buffer);
  }
  else {
    free(buffer);
    mrb_raise(mrb, E_RUNTIME_ERROR, mpg123_strerror(mh));
  }
  return result;
}

mrb_value mrb_mpg123_length(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(mpg123_length(DATA_PTR(self)));
}

mrb_value mrb_mpg123_spf(mrb_state *mrb, mrb_value self)
{
  return mrb_float_value(mrb, mpg123_spf(DATA_PTR(self)));
}

mrb_value mrb_mpg123_tpf(mrb_state *mrb, mrb_value self)
{
  return mrb_float_value(mrb, mpg123_tpf(DATA_PTR(self)));
}

mrb_value mrb_mpg123_tell(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(mpg123_tell(DATA_PTR(self)));
}

mrb_value mrb_mpg123_tellframe(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(mpg123_tellframe(DATA_PTR(self)));
}

mrb_value mrb_mpg123_seek(mrb_state *mrb, mrb_value self)
{
  mrb_value offset;
  mrb_get_args(mrb, "i", &offset);
  return mrb_fixnum_value(mpg123_seek(DATA_PTR(self), mrb_fixnum(offset), SEEK_SET));
}

mrb_value mrb_mpg123_seek_frame(mrb_state *mrb, mrb_value self)
{
  mrb_value offset;
  mrb_get_args(mrb, "i", &offset);
  return mrb_fixnum_value(mpg123_seek_frame(DATA_PTR(self), mrb_fixnum(offset), SEEK_SET));
}

mrb_value mrb_mpg123_timeframe(mrb_state *mrb, mrb_value self)
{
  mrb_value seconds;
  mrb_get_args(mrb, "i", &seconds);
  return mrb_fixnum_value(mpg123_timeframe(DATA_PTR(self), mrb_float(seconds)));
}

static int paCallback(const void *inputBuffer,
                      void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData )
{
  Portaudio *portaudio = (Portaudio *) userData;
  size_t size = framesPerBuffer * sizeof(float) * 2;
  size_t done = 0;
  int err = 0;

  if (portaudio->mpg123) {
    if (mpg123_tellframe(portaudio->mpg123) != portaudio->position) {
      mpg123_seek(portaudio->mpg123, portaudio->position, SEEK_SET);
    }

    err = mpg123_read(portaudio->mpg123, (unsigned char *) outputBuffer, size, &done);
    portaudio->rms = rms(outputBuffer, size);
    portaudio->position = mpg123_tellframe(portaudio->mpg123);

    /* switch (err) { */
    /* case MPG123_OK: break; */
    /* case MPG123_DONE: break; */
    /* } */
  }

  return 0;
}

mrb_value mrb_portaudio_new(mrb_state *mrb, mrb_value klass)
{
  PaError err;
  Portaudio *portaudio = (Portaudio *) malloc(sizeof(Portaudio));
  portaudio->mpg123 = NULL;
  portaudio->position = 0;
  portaudio->rms = 0;

  err = Pa_OpenDefaultStream(&portaudio->stream,
                             0,           /* no input channels */
                             2,           /* stereo output */
                             paFloat32,   /* 32 bit floating point output */
                             44100,       /* sample rate*/
                             4096,
                             paCallback,
                             (void*) portaudio);

  if (err != paNoError) {
    mrb_raise(mrb, E_RUNTIME_ERROR, Pa_GetErrorText(err));
  }

  return mrb_obj_value(mrb_data_object_alloc(mrb, mrb_portaudio_class, portaudio, &mrb_portaudio_data));
}


mrb_value mrb_portaudio_rms(mrb_state *mrb, mrb_value self)
{
  Portaudio *portaudio = DATA_PTR(self);
  return mrb_float_value(mrb,portaudio->rms);
}

mrb_value mrb_portaudio_start(mrb_state *mrb, mrb_value self)
{
  mrb_value mpg123;
  mrb_get_args(mrb, "o", &mpg123);
  Portaudio *portaudio = DATA_PTR(self);
  portaudio->mpg123 = mrb_check_datatype(mrb, mpg123, &mrb_mpg123_data);

  int err = Pa_StartStream(portaudio->stream);

  if (err != paNoError) {
    mrb_raise(mrb, E_RUNTIME_ERROR, Pa_GetErrorText(err));
  }

  return self;
}

mrb_value mrb_portaudio_seek(mrb_state *mrb, mrb_value self)
{
  Portaudio *portaudio = DATA_PTR(self);
  mrb_get_args(mrb, "i", &portaudio->position);
  return self;
}

mrb_value mrb_portaudio_stop(mrb_state *mrb, mrb_value self)
{
  Portaudio *portaudio = DATA_PTR(self);
  int err = Pa_StopStream(portaudio->stream);

  if (err != paNoError) {
    mrb_raise(mrb, E_RUNTIME_ERROR, Pa_GetErrorText(err));
  }

  return self;
}

void mrb_mruby_audite_gem_init(mrb_state *mrb) {
  int err = Pa_Initialize();

  if (err != paNoError) {
    mrb_raise(mrb, E_RUNTIME_ERROR, Pa_GetErrorText(err));
  }

  mrb_portaudio_class = mrb_define_class(mrb, "Portaudio", mrb->object_class);

  mrb_define_class_method(mrb, mrb_portaudio_class, "new", mrb_portaudio_new, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, mrb_portaudio_class, "rms", mrb_portaudio_rms, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_portaudio_class, "start", mrb_portaudio_start, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, mrb_portaudio_class, "stop", mrb_portaudio_stop, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_portaudio_class, "seek", mrb_portaudio_seek, MRB_ARGS_REQ(1));

  err = mpg123_init();

  if (err != MPG123_OK) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mpg123_plain_strerror(err));
  }

  mrb_mpg123_class = mrb_define_class(mrb, "Mpg123", mrb->object_class);

  mrb_define_class_method(mrb, mrb_mpg123_class, "new", mrb_mpg123_new, MRB_ARGS_REQ(1));

  mrb_define_method(mrb, mrb_mpg123_class, "close", mrb_mpg123_close, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_mpg123_class, "read", mrb_mpg123_read, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, mrb_mpg123_class, "length", mrb_mpg123_length, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_mpg123_class, "spf", mrb_mpg123_spf, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_mpg123_class, "tpf", mrb_mpg123_tpf, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_mpg123_class, "tell", mrb_mpg123_tell, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_mpg123_class, "tellframe", mrb_mpg123_tellframe, MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_mpg123_class, "seek", mrb_mpg123_seek, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, mrb_mpg123_class, "seek_frame", mrb_mpg123_seek_frame, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, mrb_mpg123_class, "timeframe", mrb_mpg123_timeframe, MRB_ARGS_REQ(1));
}

void mrb_mruby_audite_gem_final(mrb_state *mrb) {
}
