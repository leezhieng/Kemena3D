#include "ktexture.h"

namespace kemena
{
    kTexture::kTexture()
    {
        //ctor
    }

    kTexture::~kTexture()
    {
        //dtor
    }

    kTexture* kTexture::duplicate(kString newName)
    {
        // New memory, same value
        kTexture* newTexture = new kTexture(*this);
        newTexture->setTextureName(newName);

        return newTexture;
    }

    void kTexture::setTextureID(uint32_t newTextureID)
    {
        textureID = newTextureID;
    }

    uint32_t kTexture::getTextureID()
    {
        return textureID;
    }

    void kTexture::setTextureName(kString newName)
    {
        textureName = newName;
    }

    kString kTexture::getTextureName()
    {
        return textureName;
    }

    void kTexture::setType(kTextureType newType)
    {
        type = newType;
    }

    kTextureType kTexture::getType()
    {
        return type;
    }
}
