/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/debugger/JsonWriteBuffer.h"

#include "include/core/SkFlattenable.h"
#include "include/core/SkPoint.h"
#include "include/core/SkString.h"
#include "src/utils/SkJSONWriter.h"
#include "tools/debugger/DrawCommand.h"

class SkImage;
class SkMatrix;
class SkPaint;
class SkRegion;
class SkStream;
class SkTypeface;
struct SkIRect;
struct SkPoint3;
struct SkRect;

void JsonWriteBuffer::append(const char* type) {
    SkString fullName = SkStringPrintf("%02d_%s", fCount++, type);
    fWriter->appendName(fullName.c_str());
}

void JsonWriteBuffer::writePad32(const void* data, size_t size) {
    this->append("rawBytes");
    fWriter->beginArray();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        SkString hexByte = SkStringPrintf("%02x", bytes[i]);
        fWriter->appendString(hexByte);
    }
    fWriter->endArray();
}

void JsonWriteBuffer::writeByteArray(const void* data, size_t size) {
    this->append("byteArray");
    fWriter->beginArray();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        SkString hexByte = SkStringPrintf("%02x", bytes[i]);
        fWriter->appendString(hexByte);
    }
    fWriter->endArray();
}

void JsonWriteBuffer::writeBool(bool value) {
    this->append("bool");
    fWriter->appendBool(value);
}

void JsonWriteBuffer::writeScalar(SkScalar value) {
    this->append("scalar");
    fWriter->appendFloat(value);
}

void JsonWriteBuffer::writeScalarArray(SkSpan<const SkScalar> values) {
    this->append("scalarArray");
    fWriter->beginArray();
    for (auto value : values) {
        fWriter->appendFloat(value);
    }
    fWriter->endArray();
}

void JsonWriteBuffer::writeInt(int32_t value) {
    this->append("int");
    fWriter->appendS32(value);
}

void JsonWriteBuffer::writeIntArray(SkSpan<const int32_t> values) {
    this->append("intArray");
    fWriter->beginArray();
    for (auto value : values) {
        fWriter->appendS32(value);
    }
    fWriter->endArray();
}

void JsonWriteBuffer::writeUInt(uint32_t value) {
    this->append("uint");
    fWriter->appendU32(value);
}

void JsonWriteBuffer::writeString(std::string_view value) {
    this->append("string");
    fWriter->appendString(value.data(), value.size());
}

void JsonWriteBuffer::writeFlattenable(const SkFlattenable* flattenable) {
    if (flattenable) {
        this->append(flattenable->getTypeName());
        fWriter->beginObject();
        JsonWriteBuffer flattenableBuffer(fWriter, fUrlDataManager);
        flattenable->flatten(flattenableBuffer);
        fWriter->endObject();
    } else {
        this->append("flattenable");
        fWriter->appendPointer(nullptr);
    }
}

void JsonWriteBuffer::writeColor(SkColor color) {
    this->append("color");
    DrawCommand::MakeJsonColor(*fWriter, color);
}

void JsonWriteBuffer::writeColorArray(SkSpan<const SkColor> colors) {
    this->append("colorArray");
    fWriter->beginArray();
    for (auto color : colors) {
        DrawCommand::MakeJsonColor(*fWriter, color);
    }
    fWriter->endArray();
}

void JsonWriteBuffer::writeColor4f(const SkColor4f& color) {
    this->append("color");
    DrawCommand::MakeJsonColor4f(*fWriter, color);
}

void JsonWriteBuffer::writeColor4fArray(SkSpan<const SkColor4f> colors) {
    this->append("colorArray");
    fWriter->beginArray();
    for (auto color : colors) {
        DrawCommand::MakeJsonColor4f(*fWriter, color);
    }
    fWriter->endArray();
}

void JsonWriteBuffer::writePoint(const SkPoint& point) {
    this->append("point");
    DrawCommand::MakeJsonPoint(*fWriter, point);
}

void JsonWriteBuffer::writePoint3(const SkPoint3& point) {
    this->append("point3");
    DrawCommand::MakeJsonPoint3(*fWriter, point);
}

void JsonWriteBuffer::writePointArray(SkSpan<const SkPoint> points) {
    this->append("pointArray");
    fWriter->beginArray();
    for (auto point : points) {
        DrawCommand::MakeJsonPoint(*fWriter, point);
    }
    fWriter->endArray();
}

void JsonWriteBuffer::write(const SkM44& matrix) {
    this->append("matrix");
    fWriter->beginArray();
    for (int r = 0; r < 4; ++r) {
        fWriter->beginArray(nullptr, false);
        SkV4 v = matrix.row(r);
        for (int c = 0; c < 4; ++c) {
            fWriter->appendFloat(v[c]);
        }
        fWriter->endArray();
    }
    fWriter->endArray();
}

void JsonWriteBuffer::writeMatrix(const SkMatrix& matrix) {
    this->append("matrix");
    DrawCommand::MakeJsonMatrix(*fWriter, matrix);
}

void JsonWriteBuffer::writeIRect(const SkIRect& rect) {
    this->append("irect");
    DrawCommand::MakeJsonIRect(*fWriter, rect);
}

void JsonWriteBuffer::writeRect(const SkRect& rect) {
    this->append("rect");
    DrawCommand::MakeJsonRect(*fWriter, rect);
}

void JsonWriteBuffer::writeRegion(const SkRegion& region) {
    this->append("region");
    DrawCommand::MakeJsonRegion(*fWriter, region);
}

void JsonWriteBuffer::writePath(const SkPath& path) {
    this->append("path");
    DrawCommand::MakeJsonPath(*fWriter, path);
}

void JsonWriteBuffer::writeSampling(const SkSamplingOptions& sampling) {
    this->append("sampling");
    DrawCommand::MakeJsonSampling(*fWriter, sampling);
}

size_t JsonWriteBuffer::writeStream(SkStream* stream, size_t length) {
    // Contents not supported
    this->append("stream");
    fWriter->appendU64(static_cast<uint64_t>(length));
    return 0;
}

void JsonWriteBuffer::writeImage(const SkImage* image) {
    this->append("image");
    fWriter->beginObject();
    DrawCommand::flatten(*image, *fWriter, *fUrlDataManager);
    fWriter->endObject();
}

void JsonWriteBuffer::writeTypeface(SkTypeface* typeface) {
    // Unsupported
    this->append("typeface");
    fWriter->appendPointer(typeface);
}

void JsonWriteBuffer::writePaint(const SkPaint& paint) {
    this->append("paint");
    DrawCommand::MakeJsonPaint(*fWriter, paint, *fUrlDataManager);
}
