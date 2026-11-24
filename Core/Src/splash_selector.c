#include "splash_image.h"

void SplashImage_DrawSelected(void)
{
#if (SPLASH_IMAGE_SELECTION == SPLASH_IMAGE_ALTERNATE_WAVE)
  SplashImage_DrawAlternateWave();
#else
  SplashImage_Draw();
#endif
}
