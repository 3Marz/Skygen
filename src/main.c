#include <math.h>
#include <raylib.h>
#include <stdio.h>
#include "map_gen.c"



int main(void)
{
    const int screenWidth = 1000;
    const int screenHeight= 1000;

    InitWindow(screenWidth, screenHeight, "SKYGEN");

    // Voronoi Map
    VoronoiMap *v_map = initVoronoiMap((Vector2) { 1000, 1000 }, 500);
    triangulateVoronoiMap(v_map);
    relaxVoronoiMap(v_map, 2);

    // Camera setup
    Camera camera = { 0 };
    camera.target = (Vector3){ 0.0f, 5.0f, 0.0f };
    camera.position = (Vector3){ 50.0f, 40.0f, 50.0f };
    camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    int cameraMode = CAMERA_ORBITAL;

    bool update = true;
    bool switchCamera = false;
    bool isFP = false;

    // Map Texture 
    Image mapImage = genVoronoiImage(v_map);
    Texture2D mapTexture = LoadTextureFromImage(mapImage);

    // Perlin noise
    // Image noiseImage = GenImagePerlinNoise(200, 200, GetRandomValue(0, 100), GetRandomValue(0, 100), 1.0f);
    // Texture2D noiseTexture = LoadTextureFromImage(noiseImage);

    // Mesh terrainMesh = GenMeshHeightmap(noiseImage, (Vector3){ 50.0f, 30.0f, 50.0f });
    // Model terrainModel = LoadModelFromMesh(terrainMesh);

    // terrainModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = noiseTexture;

    UnloadImage(mapImage);
    // UnloadImage(noiseImage);
    //

    // for (int i = 0; i < v_map->vor.n_cells; i++) {
    //   printf("polys number: %d\n", i);
    //   printf("polys x: %f\n", v_map->vor.polys[i]->x);
    //   printf("polys y: %f\n", v_map->vor.polys[i]->y);
    //   printf("\n");
    // }

    SetTargetFPS(60);

    int regenerate = 0;

    while (!WindowShouldClose())
    {
        // Update
        UpdateCamera(&camera, cameraMode);

        if (IsKeyPressed(KEY_Z)) { switchCamera = true;  }
        if (IsKeyPressed(KEY_R)) { relaxVoronoiMap(v_map, 1); regenerate = 1; }

        if (regenerate == 1) {
          triangulateVoronoiMap(v_map);

          mapImage = genVoronoiImage(v_map);
          mapTexture = LoadTextureFromImage(mapImage);
          UnloadImage(mapImage);

          regenerate = 0;
        }

        if (switchCamera)
        {
            if (isFP)
            {
                cameraMode = CAMERA_ORBITAL;
                EnableCursor();
                isFP = false;
            } else 
            {
                cameraMode = CAMERA_FIRST_PERSON;
                DisableCursor();
                isFP = true;
            }
            camera.target = (Vector3){ 0.0f, 5.0f, 0.0f };
            camera.position = (Vector3){ 40.0f, 30.0f, 40.0f };
            switchCamera = false;
        }

        camera.position.y = 30.0f;

        // Draw
        BeginDrawing();
            ClearBackground(BLACK);

            // BeginMode3D(camera);
            //     // DrawGrid(25, 5.0f);
            //     DrawModelWires(terrainModel, (Vector3){ -25.0f, -15.0f, -25.0f }, 1.0f, WHITE);
            // EndMode3D();

            DrawTextureEx(mapTexture, (Vector2){0.0f, 0.0f}, 0.0f, 1.0f, WHITE);

            // DrawFPS(20, 20);
            // for (int i = 0; i < v_map->vor.n_cells; i++) {
            //   printf("polys number: %d\n", v_map->vor.poly_n[i]);
            //   // DrawCircleV(*v_map->vor.polys[i], 3.0f, YELLOW);
            // }

        EndDrawing();
    }

    UnloadTexture(mapTexture);
    // UnloadTexture(noiseTexture);

    // UnloadImage(noiseImage);
    // UnloadModel(terrainModel);

    freeVoronoiMap(v_map);

    CloseWindow();

    return 0;
}
