// Herramienta de HOST (corre en la maquina de build, no en la GBA). Llama a
// los mismos generadores de Textures.h (fillVault, fillDoor, fillKey, etc,
// via buildAssetsRuntime()) y vuelca el resultado como datos `const` en un
// header generado. En GBA `const` cae en ROM: cero ciclos de arranque
// generando texturas y cero bytes de .bss para guardarlas.
//
// Se ejecuta como paso de build en CMakeLists.txt, asi que no puede
// desincronizarse del codigo generador: si alguien cambia una fillX() o
// BASE_RGB, el header horneado se regenera solo en el siguiente build.
#include <cstdio>
#include <cstdint>

#include "Textures.h"

namespace {

void dumpBytes(FILE* f, const uint8_t* data, int count) {
    for (int i = 0; i < count; ++i) {
        fprintf(f, "%d,", data[i]);
        if ((i & 31) == 31) fprintf(f, "\n");
    }
}

void dumpTexture(FILE* f, const Texture& t) {
    fprintf(f, "{{");
    dumpBytes(f, t.px, TEX_SIZE * TEX_SIZE);
    fprintf(f, "}},\n");
}

void dumpSprite(FILE* f, const SpriteFrame& s) {
    fprintf(f, "{{");
    dumpBytes(f, s.px, SPR_SIZE * SPR_SIZE);
    fprintf(f, "}},\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "uso: bake_assets <archivo_de_salida.h>\n");
        return 1;
    }

    const Assets a = buildAssetsRuntime();

    FILE* f = fopen(argv[1], "w");
    if (!f) {
        fprintf(stderr, "no se pudo abrir %s para escritura\n", argv[1]);
        return 1;
    }

    fprintf(f, "// GENERADO por tools/bake_assets.cpp -- no editar a mano.\n");
    fprintf(f, "// Vuelve a generarse en cada build desde Textures.h.\n");
    fprintf(f, "#pragma once\n\n");
    fprintf(f, "inline const Assets kBakedAssets = {\n");

    fprintf(f, "  {  // tex[TEX_COUNT]\n");
    for (int t = 0; t < TEX_COUNT; ++t) dumpTexture(f, a.tex[t]);
    fprintf(f, "  },\n");

    fprintf(f, "  {  // warden[SPR_FRAMES]\n");
    for (int i = 0; i < SPR_FRAMES; ++i) dumpSprite(f, a.warden[i]);
    fprintf(f, "  },\n");

    fprintf(f, "  {  // scout[SPR_FRAMES]\n");
    for (int i = 0; i < SPR_FRAMES; ++i) dumpSprite(f, a.scout[i]);
    fprintf(f, "  },\n");

    fprintf(f, "  {  // boss[SPR_FRAMES]\n");
    for (int i = 0; i < SPR_FRAMES; ++i) dumpSprite(f, a.boss[i]);
    fprintf(f, "  },\n");

    fprintf(f, "  {  // item[ITEM_SPRITES]\n");
    for (int i = 0; i < ITEM_SPRITES; ++i) dumpSprite(f, a.item[i]);
    fprintf(f, "  },\n");

    fprintf(f, "  {  // pal[PALETTE_SIZE]\n");
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        fprintf(f, "0x%08Xu,", a.pal[i]);
        if ((i & 7) == 7) fprintf(f, "\n");
    }
    fprintf(f, "\n  },\n");

    fprintf(f, "};\n");
    fclose(f);
    return 0;
}
