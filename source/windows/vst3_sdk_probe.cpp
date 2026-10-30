// Initial native VST3 SDK compilation probe (not a plugin binary).
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
static_assert(sizeof(Steinberg::Vst::ProcessSetup) > 0);
