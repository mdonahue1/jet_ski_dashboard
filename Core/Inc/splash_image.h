#ifndef SPLASH_IMAGE_H
#define SPLASH_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

void SplashImage_Draw(void);
void SplashImage_DrawAlternateWave(void);
void SplashImage_DrawSelected(void);

#define SPLASH_IMAGE_CHUD            0U
#define SPLASH_IMAGE_ALTERNATE_WAVE  1U

/* Change this one value to select the compiled startup image. */
#ifndef SPLASH_IMAGE_SELECTION
#define SPLASH_IMAGE_SELECTION SPLASH_IMAGE_ALTERNATE_WAVE
#endif

#ifdef __cplusplus
}
#endif

#endif /* SPLASH_IMAGE_H */
