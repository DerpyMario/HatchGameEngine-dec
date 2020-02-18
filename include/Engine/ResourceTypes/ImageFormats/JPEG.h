#ifndef JPEG_H
#define JPEG_H

#define PUBLIC
#define PRIVATE
#define PROTECTED
#define STATIC
#define VIRTUAL


#include <Engine/Includes/Standard.h>
#include <Engine/ResourceTypes/ImageFormats/ImageFormat.h>
#include <Engine/IO/Stream.h>

class JPEG : public ImageFormat {
public:
    static JPEG*   Load(const char* filename);
    static bool    Save(JPEG* jpeg, const char* filename);
           bool    Save(const char* filename);
                   ~JPEG();
};

#endif /* JPEG_H */
