/*
 * Copyright 2011-2016 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "graphics/opengl/GLTexture2D.h"

#include "graphics/Math.h"
#include "graphics/opengl/GLTextureStage.h"
#include "graphics/opengl/OpenGLRenderer.h"
#include "graphics/opengl/OpenGLUtil.h"
#include "io/fs/FilePath.h" // TODO remove


GLTexture2D::GLTexture2D(OpenGLRenderer * _renderer)
	: renderer(_renderer)
	, tex(GL_NONE)
	, wrapMode(TextureStage::WrapRepeat)
	, minFilter(TextureStage::FilterLinear)
	, magFilter(TextureStage::FilterNearest)
	, isNPOT(false)
{}

GLTexture2D::~GLTexture2D() {
	Destroy();
}

bool GLTexture2D::Create() {
	
	arx_assert_msg(tex == GL_NONE, "leaking OpenGL texture");
	
	glGenTextures(1, &tex);
	
	// Set our state to the default OpenGL state
	wrapMode = TextureStage::WrapRepeat;
	minFilter = TextureStage::FilterNearest;
	magFilter = TextureStage::FilterLinear;
	
	Vec2i nextPowerOfTwo(GetNextPowerOf2(size.x), GetNextPowerOf2(size.y));
	storedSize = renderer->hasTextureNPOT() ? size : nextPowerOfTwo;
	isNPOT = (size != nextPowerOfTwo);
	
	return (tex != GL_NONE);
}

void GLTexture2D::Upload() {
	
	arx_assert(tex != GL_NONE);
	
	glBindTexture(GL_TEXTURE_2D, tex);
	renderer->GetTextureStage(0)->current = this;
	
	// I8 to L8A8
	if(!renderer->hasIntensityTextures() && (flags & Intensity)) {
		arx_assert(mFormat == Image::Format_L8);
		Image converted;
		converted.Create(storedSize.x, storedSize.y, Image::Format_L8A8);
		unsigned char * input = mImage.GetData();
		unsigned char * end = input + storedSize.x * storedSize.y;
		unsigned char * output = converted.GetData();
		for(; input != end; input++) {
			*output++ = *input;
			*output++ = *input;
		}
		mImage = converted;
		mFormat = Image::Format_L8A8;
		flags &= ~Intensity;
	}
	
	if(!renderer->hasBGRTextureTransfer()
	   && (mFormat == Image::Format_B8G8R8 || mFormat == Image::Format_B8G8R8A8)) {
		Image::Format rgbFormat = mFormat == Image::Format_B8G8R8 ? Image::Format_R8G8B8 : Image::Format_R8G8B8A8;
		mImage.ConvertTo(rgbFormat);
		mFormat = rgbFormat;
	}
	
	GLint internalUnsized, internalSized;
	GLenum format;
	if(flags & Intensity) {
		internalUnsized = GL_INTENSITY, internalSized = GL_INTENSITY8, format = GL_RED;
	} else if(mFormat == Image::Format_L8) {
		internalUnsized = GL_LUMINANCE, internalSized = GL_LUMINANCE8, format = GL_LUMINANCE;
	} else if(mFormat == Image::Format_A8) {
		internalUnsized = GL_ALPHA, internalSized = GL_ALPHA8, format = GL_ALPHA;
	} else if(mFormat == Image::Format_L8A8) {
		internalUnsized = GL_LUMINANCE_ALPHA, internalSized = GL_LUMINANCE8_ALPHA8, format = GL_LUMINANCE_ALPHA;
	} else if(mFormat == Image::Format_R8G8B8) {
		internalUnsized = GL_RGB, internalSized = GL_RGB8, format = GL_RGB;
	} else if(mFormat == Image::Format_B8G8R8) {
		internalUnsized = GL_RGB, internalSized = GL_RGB8, format = GL_BGR;
	} else if(mFormat == Image::Format_R8G8B8A8) {
		internalUnsized = GL_RGBA, internalSized = GL_RGBA8, format = GL_RGBA;
	} else if(mFormat == Image::Format_B8G8R8A8) {
		internalUnsized = GL_RGBA, internalSized = GL_RGBA8, format = GL_BGRA;
	} else {
		arx_assert_msg(false, "Unsupported image format: %ld", long(mFormat));
		return;
	}
	GLint internal = renderer->hasSizedTextureFormats() ? internalSized : internalUnsized;
	
	if(storedSize != size) {
		flags &= ~HasMipmaps;
	}
	
	if(hasMipmaps()) {
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		if(renderer->getMaxAnisotropy() > 1.f) {
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, renderer->getMaxAnisotropy());
		}
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}
	
	// TODO handle GL_MAX_TEXTURE_SIZE
	
	if(storedSize != size) {
		Image extended;
		extended.Create(storedSize.x, storedSize.y, mImage.GetFormat());
		extended.extendClampToEdgeBorder(mImage);
		glTexImage2D(GL_TEXTURE_2D, 0, internal, storedSize.x, storedSize.y, 0, format,
		             GL_UNSIGNED_BYTE, extended.GetData());
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, internal, size.x, size.y, 0, format,
		             GL_UNSIGNED_BYTE, mImage.GetData());
	}
	
}

void GLTexture2D::Destroy() {
	
	if(tex) {
		glDeleteTextures(1, &tex), tex = GL_NONE;
	}
	
	for(size_t i = 0; i < renderer->GetTextureStageCount(); i++) {
		GLTextureStage * stage = renderer->GetTextureStage(i);
		if(stage->tex == this) {
			stage->tex = NULL;
		}
		if(stage->current == this) {
			stage->current = NULL;
		}
	}
}

static const GLint arxToGlWrapMode[] = {
	GL_REPEAT, // WrapRepeat,
	GL_MIRRORED_REPEAT, // WrapMirror
	GL_CLAMP_TO_EDGE // WrapClamp
};

static const GLint arxToGlFilter[][2] = {
	// no mipmap
	{
		GL_NEAREST, // FilterNearest
		GL_LINEAR   // FilterLinear
	},
	// mipmap
	{
		GL_NEAREST_MIPMAP_LINEAR, // FilterNearest TODO does GL_NEAREST_MIPMAP_NEAREST make more sense?
		GL_LINEAR_MIPMAP_LINEAR   // FilterLinear
	}
};

void GLTexture2D::apply(GLTextureStage * stage) {
	
	arx_assert(stage != NULL);
	arx_assert(stage->tex == this);
	
	// TODO: Fix callers and change this into an assert/error/warning.
	TextureStage::WrapMode newWrapMode = (!isNPOT) ? stage->wrapMode
	                                               : TextureStage::WrapClamp;
	if(newWrapMode != wrapMode) {
		wrapMode = newWrapMode;
		GLint glwrap = arxToGlWrapMode[wrapMode];
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glwrap);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glwrap);
	}
	
	if(stage->minFilter != minFilter) {
		minFilter = stage->minFilter;
		int mipFilter = hasMipmaps() ? 1 : 0;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, arxToGlFilter[mipFilter][minFilter]);
	}
	
	if(stage->magFilter != magFilter) {
		magFilter = stage->magFilter;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, arxToGlFilter[0][magFilter]);
	}
	
}

void GLTexture2D::updateMaxAnisotropy() {
	
	if(hasMipmaps()) {
		glBindTexture(GL_TEXTURE_2D, tex);
		renderer->GetTextureStage(0)->current = this;
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, renderer->getMaxAnisotropy());
	}
	
}
