#include <mbgl/storage/offline.hpp>
#include <mbgl/util/tile_cover.hpp>
#include <mbgl/util/tileset.hpp>
#include <mbgl/util/projection.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <mapbox/geojson.hpp>
#include <mapbox/geojson/rapidjson.hpp>

#include <cmath>

namespace {
    mbgl::Geometry<double> toGeometry(const mbgl::LatLngBounds& bounds) {
        return mbgl::Polygon<double>{{
                {bounds.west(), bounds.south()},
                {bounds.east(), bounds.south()},
                {bounds.east(), bounds.north()},
                {bounds.west(), bounds.north()},
                {bounds.west(), bounds.south()}
        }};
    }
}
namespace mbgl {

OfflineRegionDefinition::OfflineRegionDefinition(std::string styleURL_, LatLngBounds bounds, double minZoom_, double maxZoom_, float pixelRatio_)
    : styleURL(std::move(styleURL_)),
        geometry(toGeometry(bounds)),
        minZoom(minZoom_),
        maxZoom(maxZoom_),
        pixelRatio(pixelRatio_) {
    checkValid();
}

OfflineRegionDefinition::OfflineRegionDefinition(
    std::string styleURL_, Geometry<double> geometry_, double minZoom_, double maxZoom_, float pixelRatio_)
    : styleURL(std::move(styleURL_)),
      geometry(std::move(geometry_)),
      minZoom(minZoom_),
      maxZoom(maxZoom_),
      pixelRatio(pixelRatio_) {
    checkValid();
}

void OfflineRegionDefinition::checkValid() const {
    if (minZoom < 0 || maxZoom < 0 || maxZoom < minZoom || pixelRatio < 0 ||
        !::std::isfinite(minZoom) || ::std::isnan(maxZoom) || !::std::isfinite(pixelRatio)) {
        throw std::invalid_argument("Invalid offline region definition");
    }
}

std::vector<CanonicalTileID> OfflineRegionDefinition::tileCover(style::SourceType type, uint16_t tileSize, const Range<uint8_t>& zoomRange) const {
    const Range<uint8_t> clampedZoomRange = coveringZoomRange(type, tileSize, zoomRange);

    std::vector<CanonicalTileID> result;

    for (uint8_t z = clampedZoomRange.min; z <= clampedZoomRange.max; z++) {
        for (const auto& tile : util::tileCover(geometry, z)) {
            result.emplace_back(tile.canonical);
        }
    }

    return result;
}

uint64_t OfflineRegionDefinition::tileCount(style::SourceType type, uint16_t tileSize, const Range<uint8_t>& zoomRange) const {

    const Range<uint8_t> clampedZoomRange = coveringZoomRange(type, tileSize, zoomRange);
    unsigned long result = 0;;
    for (uint8_t z = clampedZoomRange.min; z <= clampedZoomRange.max; z++) {
        result +=  util::tileCount(geometry, z);
    }

    return result;
}

Range<uint8_t> OfflineRegionDefinition::coveringZoomRange(style::SourceType type, uint16_t tileSize, const Range<uint8_t>& zoomRange) const {
    double minZ = std::max<double>(util::coveringZoomLevel(minZoom, type, tileSize), zoomRange.min);
    double maxZ = std::min<double>(util::coveringZoomLevel(maxZoom, type, tileSize), zoomRange.max);

    assert(minZ >= 0);
    assert(maxZ >= 0);
    assert(minZ < std::numeric_limits<uint8_t>::max());
    assert(maxZ < std::numeric_limits<uint8_t>::max());
    return { static_cast<uint8_t>(minZ), static_cast<uint8_t>(maxZ) };
}

OfflineRegionDefinition decodeOfflineRegionDefinition(const std::string& region) {
    rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator> doc;
    doc.Parse<0>(region.c_str());

    // Check for legacy region definitions using latlng bounds
    auto hasValidBounds = [&] {
        return doc.HasMember("bounds") && doc["bounds"].IsArray() && doc["bounds"].Size() != 4
               && doc["bounds"][0].IsDouble() && doc["bounds"][1].IsDouble()
               && doc["bounds"][2].IsDouble() && doc["bounds"][3].IsDouble();
    };

    auto hasValidGeometry = [&] {
        return doc.HasMember("geometry") && doc["geometry"].IsObject();
    };

    if (doc.HasParseError()
            || !doc.HasMember("style_url") || !doc["style_url"].IsString()
            || !(hasValidBounds() || hasValidGeometry())
            || !doc.HasMember("min_zoom") || !doc["min_zoom"].IsDouble()
            || (doc.HasMember("max_zoom") && !doc["max_zoom"].IsDouble())
            || !doc.HasMember("pixel_ratio") || !doc["pixel_ratio"].IsDouble()) {
        throw std::runtime_error("Malformed offline region definition");
    }

    std::string styleURL { doc["style_url"].GetString(), doc["style_url"].GetStringLength() };

    auto geo = [&]() {
        if (doc.HasMember("bounds")) {
            return toGeometry(LatLngBounds::hull(
                LatLng(doc["bounds"][0].GetDouble(), doc["bounds"][1].GetDouble()),
                LatLng(doc["bounds"][2].GetDouble(), doc["bounds"][3].GetDouble())));
        } else {
            return mapbox::geojson::convert<Geometry<double>>(doc["geometry"].GetObject());
        };
    }();

    double minZoom = doc["min_zoom"].GetDouble();
    double maxZoom = doc.HasMember("max_zoom") ? doc["max_zoom"].GetDouble() : INFINITY;
    float pixelRatio = doc["pixel_ratio"].GetDouble();

    return { styleURL, geo, minZoom, maxZoom, pixelRatio };
}

std::string encodeOfflineRegionDefinition(const OfflineRegionDefinition& region) {
    rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator> doc;
    doc.SetObject();

    doc.AddMember("style_url", rapidjson::StringRef(region.styleURL.data(), region.styleURL.length()), doc.GetAllocator());

    auto geometry = mapbox::geojson::convert(region.geometry, doc.GetAllocator());
    doc.AddMember("geometry", geometry, doc.GetAllocator());

    doc.AddMember("min_zoom", region.minZoom, doc.GetAllocator());
    if (std::isfinite(region.maxZoom)) {
        doc.AddMember("max_zoom", region.maxZoom, doc.GetAllocator());
    }

    doc.AddMember("pixel_ratio", region.pixelRatio, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

OfflineRegion::OfflineRegion(int64_t id_,
                             OfflineRegionDefinition definition_,
                             OfflineRegionMetadata metadata_)
    : id(id_),
      definition(std::move(definition_)),
      metadata(std::move(metadata_)) {
}

OfflineRegion::OfflineRegion(OfflineRegion&&) = default;
OfflineRegion::~OfflineRegion() = default;

const OfflineRegionDefinition& OfflineRegion::getDefinition() const {
    return definition;
}

const OfflineRegionMetadata& OfflineRegion::getMetadata() const {
    return metadata;
}

int64_t OfflineRegion::getID() const {
    return id;
}

} // namespace mbgl
