#define SOKOL_GLCORE

#include "sokol_app.h"
#include "sokol_audio.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"

// Workaround for minor incompatibility (ref. edubart/sokol_gp#32)
#define SG_BACKEND_GLCORE33 SG_BACKEND_GLCORE
#include "sokol_gp.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#include "stb_image.h"
