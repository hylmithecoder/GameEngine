/* Single translation unit that emits the stb_image implementation.
 *
 * Previously each consumer defined STB_IMAGE_IMPLEMENTATION itself, which was
 * both fragile (texture.cpp defined it *after* the #include, so it emitted
 * nothing and ViewPort3D failed to link) and a duplicate-symbol hazard once
 * more than one TU got it right. Compiling it here once and linking the
 * resulting library everywhere keeps every target consistent.
 */
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
