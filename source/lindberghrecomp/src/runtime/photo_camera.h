#ifndef HOTD4_PHOTO_CAMERA_H
#define HOTD4_PHOTO_CAMERA_H

typedef struct {
    float position[3];
    float yaw, pitch, roll, zoom_stops;
    int hide_hud;
} PhotoCamera;

void hotd4_photo_register(int (*available)(void), int (*render)(const PhotoCamera *));

#endif
