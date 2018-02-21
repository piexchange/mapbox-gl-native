#include <mbgl/util/tile_cover.hpp>
#include <mbgl/util/constants.hpp>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/map/transform_state.hpp>

#include <functional>

namespace mbgl {

namespace {

// Taken from polymaps src/Layer.js
// https://github.com/simplegeo/polymaps/blob/master/src/Layer.js#L333-L383
struct edge {
    double x0 = 0, y0 = 0;
    double x1 = 0, y1 = 0;
    double dx = 0, dy = 0;

    edge(Point<double> a, Point<double> b) {
        if (a.y > b.y) std::swap(a, b);
        x0 = a.x;
        y0 = a.y;
        x1 = b.x;
        y1 = b.y;
        dx = b.x - a.x;
        dy = b.y - a.y;
    }
};

using ScanLine = const std::function<void(int32_t x0, int32_t x1, int32_t y)>;

// scan-line conversion
static void scanSpans(edge e0, edge e1, int32_t ymin, int32_t ymax, ScanLine scanLine) {
    double y0 = ::fmax(ymin, std::floor(e1.y0));
    double y1 = ::fmin(ymax, std::ceil(e1.y1));

    // sort edges by x-coordinate
    if ((e0.x0 == e1.x0 && e0.y0 == e1.y0) ?
        (e0.x0 + e1.dy / e0.dy * e0.dx < e1.x1) :
        (e0.x1 - e1.dy / e0.dy * e0.dx < e1.x0)) {
        std::swap(e0, e1);
    }

    // scan lines!
    double m0 = e0.dx / e0.dy;
    double m1 = e1.dx / e1.dy;
    double d0 = e0.dx > 0; // use y + 1 to compute x0
    double d1 = e1.dx < 0; // use y + 1 to compute x1
    for (int32_t y = y0; y < y1; y++) {
        double x0 = m0 * ::fmax(0, ::fmin(e0.dy, y + d0 - e0.y0)) + e0.x0;
        double x1 = m1 * ::fmax(0, ::fmin(e1.dy, y + d1 - e1.y0)) + e1.x0;
        scanLine(std::floor(x1), std::ceil(x0), y);
    }
}

// scan-line conversion
static void scanTriangle(const Point<double>& a, const Point<double>& b, const Point<double>& c, int32_t ymin, int32_t ymax, ScanLine& scanLine) {
    edge ab = edge(a, b);
    edge bc = edge(b, c);
    edge ca = edge(c, a);

    // sort edges by y-length
    if (ab.dy > bc.dy) { std::swap(ab, bc); }
    if (ab.dy > ca.dy) { std::swap(ab, ca); }
    if (bc.dy > ca.dy) { std::swap(bc, ca); }

    // scan span! scan span!
    if (ab.dy) scanSpans(ca, ab, ymin, ymax, scanLine);
    if (bc.dy) scanSpans(ca, bc, ymin, ymax, scanLine);
}

} // namespace

namespace util {

namespace {

std::vector<UnwrappedTileID> tileCover(const Point<double>& tl,
                                       const Point<double>& tr,
                                       const Point<double>& br,
                                       const Point<double>& bl,
                                       const Point<double>& c,
                                       int32_t z) {
    const int32_t tiles = 1 << z;

    struct ID {
        int32_t x, y;
        double sqDist;
    };

    std::vector<ID> t;

    auto scanLine = [&](int32_t x0, int32_t x1, int32_t y) {
        int32_t x;
        if (y >= 0 && y <= tiles) {
            for (x = x0; x < x1; ++x) {
                const auto dx = x + 0.5 - c.x, dy = y + 0.5 - c.y;
                t.emplace_back(ID{ x, y, dx * dx + dy * dy });
            }
        }
    };

    // Divide the screen up in two triangles and scan each of them:
    // \---+
    // | \ |
    // +---\.
    scanTriangle(tl, tr, br, 0, tiles, scanLine);
    scanTriangle(br, bl, tl, 0, tiles, scanLine);

    // Sort first by distance, then by x/y.
    std::sort(t.begin(), t.end(), [](const ID& a, const ID& b) {
        return std::tie(a.sqDist, a.x, a.y) < std::tie(b.sqDist, b.x, b.y);
    });

    // Erase duplicate tile IDs (they typically occur at the common side of both triangles).
    t.erase(std::unique(t.begin(), t.end(), [](const ID& a, const ID& b) {
                return a.x == b.x && a.y == b.y;
            }), t.end());

    std::vector<UnwrappedTileID> result;
    for (const auto& id : t) {
        result.emplace_back(z, id.x, id.y);
    }
    return result;
}

//Ported from tile-cover js lib. Uses Fast Voxel Traversal
std::vector<Point<int32_t>> lineCover(const std::vector<Point<double>>& coords,
                                      int32_t zoom,
                                      std::vector<Point<int32_t>>* ring = nullptr) {
    std::vector<Point<int32_t>> tiles;
    auto numPoints = coords.size();
    
    auto prevPoint = TileCoordinate::fromLatLng(zoom,{ coords[0].y, coords[0].x }).p;
    int32_t prevX = INT_MAX, prevY = INT_MAX;
    int32_t x,y;
    for (uint32_t i=1; i < numPoints; i++) {
        Point<double> p0 = prevPoint;
        Point<double> p1 = TileCoordinate::fromLatLng(zoom, { coords[i].y, coords[i].x}).p;
        prevPoint = p1;

        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        if (dx == 0 && dy == 0) continue;

        const auto xi = dx > 0 ? 1 : -1;
        const auto yi = dy > 0 ? 1 : -1;

        x = static_cast<int32_t>(floor(p0.x));
        y = static_cast<int32_t>(floor(p0.y));

        auto tMaxX = dx == 0 ? INT_MAX : std::abs(((dx > 0 ? 1 : 0) + x - p0.x)/dx);
        auto tMaxY = dy == 0 ? INT_MAX : std::abs(((dy > 0 ? 1 : 0) + y - p0.y)/dy);

        const auto tdx = std::abs(xi/dx);
        const auto tdy = std::abs(yi/dy);

        if (prevX != x || prevY != y) {
            tiles.emplace_back(x, y);
            if (ring && y != prevY) {
                ring->emplace_back(x, y);
            }
            prevX = x; prevY = y;
        }

        while (tMaxX < 1 || tMaxY < 1) {
            if (tMaxX < tMaxY) {
                tMaxX += tdx;
                x += xi;
            } else {
                tMaxY += tdy;
                y += yi;
            }
            tiles.emplace_back(x, y);
            if (ring && y != prevY) {
                ring->emplace_back(x, y);
            }
            prevX = x; prevY = y;
        }
    }

    if (ring && y == ring->at(0).y) {
        ring->pop_back();
    }

    return tiles;
}

std::vector<Point<int32_t>> polygonCover(const Polygon<double>& geom, int32_t zoom) {
    std::vector<Point<int32_t>> tiles, intersections;

    for (uint32_t i = 0; i < geom.size(); i++) {
        std::vector<Point<int32_t>> ring;
        auto cover = lineCover(geom[i], zoom, &ring);

        tiles.insert(tiles.end(), cover.begin(), cover.end());
        uint32_t ringLength = ring.size();
        uint32_t k = ringLength - 1;
        for (uint32_t j = 0; j < ringLength; k = j++) {
            auto m = (j + 1) % ringLength;
            auto y = ring[j].y;

            // add interesction if it's not local extremum or duplicate
            if ((y > ring[k].y || y > ring[m].y) && // not local minimum
                (y < ring[k].y || y < ring[m].y) && // not local maximum
                (y!= ring[m].y)) {
                intersections.push_back(ring[j]);
            }
        }
    }

    // Sort by y then by x
    std::sort(intersections.begin(), intersections.end(), [](const Point<int32_t>& a, const Point<int32_t>& b) {
        return std::tie(a.y, a.x) < std::tie(b.y, b.x);
    });

    //Fill tiles inside the ring
    auto numTiles = intersections.size();
    for(uint32_t i = 0; i < numTiles; i+=2) {
        auto t = intersections[i];
        auto t1 = intersections[i+1];
        auto y = t.y;
        for (int32_t x = t.x + 1; x < t1.x; x++) {
            tiles.emplace_back(x, y);
        }
    }
    return tiles;
}

struct ToTileCover {
    int32_t zoom;
    ToTileCover(int32_t z): zoom(z) {}

    std::vector<Point<int32_t>> operator()(const Point<double>& g) const {
        auto projectedPt = TileCoordinate::fromLatLng(zoom, { g.y, g.x }).p;
        return {{ static_cast<int32_t>(floor(projectedPt.x)),
                  static_cast<int32_t>(floor(projectedPt.y)) }};
    }
    std::vector<Point<int32_t>> operator()(const MultiPoint<double>& g) const {
        std::vector<Point<int32_t>> tiles;
        for (auto& pt: g) {
            auto projectedPt = TileCoordinate::fromLatLng(zoom, { pt.y, pt.x }).p;
            tiles.emplace_back(static_cast<int32_t>(floor(projectedPt.x)),
                               static_cast<int32_t>(floor(projectedPt.y)));
        };
        return tiles;
    }
    std::vector<Point<int32_t>> operator()(const LineString<double>& g) const {
        return lineCover(g, zoom);
    }
    std::vector<Point<int32_t>> operator()(const MultiLineString<double>& g) const {
        std::vector<Point<int32_t>> tiles;
        for(auto& line: g) {
            auto t = lineCover(line, zoom);
            tiles.insert(tiles.end(), t.begin(), t.end());
        }
        return tiles;
    }
    std::vector<Point<int32_t>> operator()(const Polygon<double>& g) const {
        return polygonCover(g, zoom);
    }
    std::vector<Point<int32_t>> operator()(const MultiPolygon<double>& g) const {
        std::vector<Point<int32_t>> tiles;
        for(auto& poly: g) {
            auto t = polygonCover(poly, zoom);
            tiles.insert(tiles.end(), t.begin(), t.end());
        }
        return tiles;
    }
    std::vector<Point<int32_t>> operator()(const mapbox::geometry::geometry_collection<double>& g) const {
        std::vector<Point<int32_t>> tiles;
        for(auto& geometry: g) {
            auto t = apply_visitor(ToTileCover(zoom), geometry);
            tiles.insert(tiles.end(), t.begin(), t.end());
        }
        return tiles;
    }
};

} // namespace

int32_t coveringZoomLevel(double zoom, style::SourceType type, uint16_t size) {
    zoom += std::log(util::tileSize / size) / std::log(2);
    if (type == style::SourceType::Raster || type == style::SourceType::Video) {
        return ::round(zoom);
    } else {
        return std::floor(zoom);
    }
}

std::vector<UnwrappedTileID> tileCover(const LatLngBounds& bounds_, int32_t z) {
    if (bounds_.isEmpty() ||
        bounds_.south() >  util::LATITUDE_MAX ||
        bounds_.north() < -util::LATITUDE_MAX) {
        return {};
    }

    LatLngBounds bounds = LatLngBounds::hull(
        { std::max(bounds_.south(), -util::LATITUDE_MAX), bounds_.west() },
        { std::min(bounds_.north(),  util::LATITUDE_MAX), bounds_.east() });

    return tileCover(
        Projection::project(bounds.northwest(), z),
        Projection::project(bounds.northeast(), z),
        Projection::project(bounds.southeast(), z),
        Projection::project(bounds.southwest(), z),
        Projection::project(bounds.center(), z),
        z);
}

std::vector<UnwrappedTileID> tileCover(const Geometry<double>& geom, int32_t zoom ) {
    ToTileCover ttc(zoom);
    std::vector<Point<int32_t>> tiles = apply_visitor(ttc, geom);

    // Sort by y then by x
    std::sort(tiles.begin(), tiles.end(), [](const auto& a, const auto& b) {
        return std::tie(a.y, a.x) < std::tie(b.y, b.x);
    });

    // Erase duplicate tile IDs
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());

    std::vector<UnwrappedTileID> tileIds;
    tileIds.reserve(tiles.size());
    for (const auto& t: tiles) {
        tileIds.emplace_back(zoom, t.x, t.y);
    }
    return tileIds;
}

std::vector<UnwrappedTileID> tileCover(const TransformState& state, int32_t z) {
    assert(state.valid());

    const double w = state.getSize().width;
    const double h = state.getSize().height;
    return tileCover(
        TileCoordinate::fromScreenCoordinate(state, z, { 0,   0   }).p,
        TileCoordinate::fromScreenCoordinate(state, z, { w,   0   }).p,
        TileCoordinate::fromScreenCoordinate(state, z, { w,   h   }).p,
        TileCoordinate::fromScreenCoordinate(state, z, { 0,   h   }).p,
        TileCoordinate::fromScreenCoordinate(state, z, { w/2, h/2 }).p,
        z);
}

uint64_t tileCount(const Geometry<double>& geom, uint8_t zoom){
    return tileCover(geom, zoom).size();
}

// Taken from https://github.com/mapbox/sphericalmercator#xyzbbox-zoom-tms_style-srs
// Computes the projected tiles for the lower left and upper right points of the bounds
// and uses that to compute the tile cover count
uint64_t tileCount(const LatLngBounds& bounds, uint8_t zoom){
    if (zoom == 0) {
        return 1;
    }
    auto sw = Projection::project(bounds.southwest(), zoom);
    auto ne = Projection::project(bounds.northeast(), zoom);
    auto maxTile = std::pow(2.0, zoom);
    auto x1 = floor(sw.x);
    auto x2 = ceil(ne.x) - 1;
    auto y1 = util::clamp(floor(sw.y), 0.0, maxTile - 1);
    auto y2 = util::clamp(floor(ne.y), 0.0, maxTile - 1);

    auto dx = x1 > x2 ? (maxTile - x1) + x2 : x2 - x1;
    auto dy = y1 - y2;
    return (dx + 1) * (dy + 1);
}


} // namespace util
} // namespace mbgl
