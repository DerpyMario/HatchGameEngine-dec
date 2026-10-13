#ifndef ENGINE_EXPORTERS_SEGAVDPCONVERTER_H
#define ENGINE_EXPORTERS_SEGAVDPCONVERTER_H

class TileLayer;

#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/SegaVDPTypes.h>

class SegaVDPConverter {
private:
    static Uint32 QuantizeColor(Uint32 native);
    static long ColorDistance(Uint32 a, Uint32 b);
    static void BuildCells(TileLayer* layer, int planeW, int planeH);
    static void CellColors(SegaVDPCell* cell, vector<Uint32>* out);
    static int MissingFrom(SegaVDPPalette* palette, vector<Uint32>* colors);
    static void AssignPalettes();
    static void PackCell(SegaVDPCell* cell, SegaVDPTile* out);
    static void FlipTile(SegaVDPTile* in, bool flipX, bool flipY, SegaVDPTile* out);
    static int AddPattern(SegaVDPTile* tile, bool* flipX, bool* flipY);

public:
    static void ChoosePlaneSize(int wantW, int wantH, int* planeW, int* planeH);
    static bool Convert(TileLayer* layer, int planeW, int planeH, SegaVDPConversion* out);
    static void PaletteBytes(vector<Uint8>* out);
    static void TileBytes(vector<Uint8>* out);
    static void NametableBytes(vector<Uint8>* out);
};

#endif /* ENGINE_EXPORTERS_SEGAVDPCONVERTER_H */
