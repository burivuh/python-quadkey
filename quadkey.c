
#include <Python.h>
#include <math.h>


#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
typedef unsigned long long uint64;
typedef unsigned int uint32;

#define MAX_ZOOM 31

#define MAX_LONGITUDE 180.0
#define MAX_LATITUDE  85.05112877980659  /* (2*atan(exp(M_PI))*180.0/M_PI - 90.0) */

#define MIN_LONGITUDE (-MAX_LONGITUDE)
#define MIN_LATITUDE (-MAX_LATITUDE)

#define WEBMERCATOR_R 6378137.0

#define XY_SCALE 2147483648.0 /* (double)((uint32)1 << MAX_ZOOM) */
#define INV_XY_SCALE (1.0/XY_SCALE)
#define WM_RANGE (2.0*M_PI*WEBMERCATOR_R)
#define INV_WM_RANGE (1.0/WM_RANGE)
#define WM_MAX (M_PI*WEBMERCATOR_R)

static inline uint64 xy2quadint(uint64 x, uint64 y) {

    static uint64 B[] = { 0x5555555555555555, 0x3333333333333333, 0x0F0F0F0F0F0F0F0F, 0x00FF00FF00FF00FF, 0x0000FFFF0000FFFF };
    static uint64 S[] = { 1, 2, 4, 8, 16 };

    x = (x | (x << S[4])) & B[4];
    y = (y | (y << S[4])) & B[4];

    x = (x | (x << S[3])) & B[3];
    y = (y | (y << S[3])) & B[3];

    x = (x | (x << S[2])) & B[2];
    y = (y | (y << S[2])) & B[2];

    x = (x | (x << S[1])) & B[1];
    y = (y | (y << S[1])) & B[1];

    x = (x | (x << S[0])) & B[0];
    y = (y | (y << S[0])) & B[0];

    return x | (y << 1);
}

static inline void  quadint2xy(uint64 quadint, uint32* result_x, uint32* result_y)
{
    static const uint64 B[] = {
        0x5555555555555555, 0x3333333333333333, 0x0F0F0F0F0F0F0F0F, 0x00FF00FF00FF00FF, 0x0000FFFF0000FFFF,
        0x00000000FFFFFFFF
    };
    static const unsigned int S[] =
        { 0, 1, 2, 4, 8, 16 };

    uint64 x = quadint;
    uint64 y = quadint >> 1;

    x = (x | (x >> S[0])) & B[0];
    y = (y | (y >> S[0])) & B[0];

    x = (x | (x >> S[1])) & B[1];
    y = (y | (y >> S[1])) & B[1];

    x = (x | (x >> S[2])) & B[2];
    y = (y | (y >> S[2])) & B[2];

    x = (x | (x >> S[3])) & B[3];
    y = (y | (y >> S[3])) & B[3];

    x = (x | (x >> S[4])) & B[4];
    y = (y | (y >> S[4])) & B[4];

    x = (x | (x >> S[5])) & B[5];
    y = (y | (y >> S[5])) & B[5];

    *result_x = x;
    *result_y = y;
}

static inline uint64 tile_prefix_mask(int zoom) {
    static const uint64 masks[] = {
        0x0ULL,
        0x3000000000000000ULL,
        0x3c00000000000000ULL,
        0x3f00000000000000ULL,
        0x3fc0000000000000ULL,
        0x3ff0000000000000ULL,
        0x3ffc000000000000ULL,
        0x3fff000000000000ULL,
        0x3fffc00000000000ULL,
        0x3ffff00000000000ULL,
        0x3ffffc0000000000ULL,
        0x3fffff0000000000ULL,
        0x3fffffc000000000ULL,
        0x3ffffff000000000ULL,
        0x3ffffffc00000000ULL,
        0x3fffffff00000000ULL,
        0x3fffffffc0000000ULL,
        0x3ffffffff0000000ULL,
        0x3ffffffffc000000ULL,
        0x3fffffffff000000ULL,
        0x3fffffffffc00000ULL,
        0x3ffffffffff00000ULL,
        0x3ffffffffffc0000ULL,
        0x3fffffffffff0000ULL,
        0x3fffffffffffc000ULL,
        0x3ffffffffffff000ULL,
        0x3ffffffffffffc00ULL,
        0x3fffffffffffff00ULL,
        0x3fffffffffffffc0ULL,
        0x3ffffffffffffff0ULL,
        0x3ffffffffffffffcULL,
        0x3fffffffffffffffULL
    };
    if (zoom < 0)
      zoom = 0;
    else if (zoom > MAX_ZOOM)
      zoom = MAX_ZOOM;
    return masks[zoom];
}

static inline uint64 tile_suffix_mask(int zoom) {
    static const uint64 masks[] = {
        0x3fffffffffffffffULL,
        0xfffffffffffffffULL,
        0x3ffffffffffffffULL,
        0xffffffffffffffULL,
        0x3fffffffffffffULL,
        0xfffffffffffffULL,
        0x3ffffffffffffULL,
        0xffffffffffffULL,
        0x3fffffffffffULL,
        0xfffffffffffULL,
        0x3ffffffffffULL,
        0xffffffffffULL,
        0x3fffffffffULL,
        0xfffffffffULL,
        0x3ffffffffULL,
        0xffffffffULL,
        0x3fffffffULL,
        0xfffffffULL,
        0x3ffffffULL,
        0xffffffULL,
        0x3fffffULL,
        0xfffffULL,
        0x3ffffULL,
        0xffffULL,
        0x3fffULL,
        0xfffULL,
        0x3ffULL,
        0xffULL,
        0x3fULL,
        0xfULL,
        0x3ULL,
        0x0ULL
    };
    if (zoom < 0)
      zoom = 0;
    else if (zoom > MAX_ZOOM)
      zoom = MAX_ZOOM;
    return masks[zoom];
}

void lonlat2xy(double lon, double lat, int zoom, uint32* x, uint32* y) {

  lon = MIN(MAX_LONGITUDE, MAX(MIN_LONGITUDE, lon));
  lat = MIN(MAX_LATITUDE, MAX(MIN_LATITUDE, lat));

  double fx = (lon+180.0)/360.0;
  double sinlat = sin(lat * M_PI/180.0);
  double fy = 0.5 - log((1+sinlat)/(1-sinlat)) / (4*M_PI);

  uint32 mapsize = (1 << zoom);
  *x = (uint32)floor(fx*mapsize);
  *y = (uint32)floor(fy*mapsize);
  *x = MIN(mapsize - 1, MAX(0, *x));
  *y = MIN(mapsize - 1, MAX(0, *y));
}

void xy2webmercator(uint32 x, uint32 y, double* wm_x, double* wm_y) {
   *wm_x = (x*INV_XY_SCALE - 0.5)*WM_RANGE;
   *wm_y = (0.5 - y*INV_XY_SCALE)*WM_RANGE;
}

void webmercator2xy(double wm_x, double wm_y, uint32* x, uint32* y) {
   *x = (wm_x*INV_WM_RANGE + 0.5)*XY_SCALE;
   *y = (0.5 - wm_y*INV_WM_RANGE)*XY_SCALE;
}

uint64 lonlat2quadint(double lon, double lat) {
    uint32 x, y;
    lonlat2xy(lon, lat, MAX_ZOOM, &x, &y);
    return xy2quadint(x, y);
}

void tile2bbox_scaled(double scale_x, double scale_y, double offset_x, double offset_y, uint64 quadint, int zoom, double* x_min, double* y_min, double* x_max, double* y_max) {
  unsigned int x, y;
  int zero_bits = MAX_ZOOM - zoom;
  quadint2xy(quadint, &x, &y);
  x >>= zero_bits;
  y >>= zero_bits;
  *x_min = offset_x + (x * 1.0 / (1ull << zoom)) * scale_x;
  *x_max = offset_x + ((x + 1) * 1.0 / (1ull << zoom)) * scale_x;
  *y_min = offset_y + ((y + 1) * 1.0 / (1ull << zoom)) * scale_y;
  *y_max = offset_y + (y * 1.0 / (1ull << zoom)) * scale_y;
}

void tile2bbox_webmercator(uint64 quadint, int zoom, double* x_min, double* y_min, double* x_max, double* y_max) {
  tile2bbox_scaled(WM_RANGE, -WM_RANGE, -WM_MAX, WM_MAX, quadint, zoom, x_min, y_min, x_max, y_max);
}

void tile2bbox(uint64 quadint, int zoom, double* lon_min, double* lat_min, double* lon_max, double* lat_max) {
  double x_min, y_min, x_max, y_max;
  tile2bbox_scaled(1.0, 1.0, -0.5, -0.5, quadint, zoom, &x_min, &y_min, &x_max, &y_max);

  *lon_min = 360.0 * x_min;
  *lon_max = 360.0 * x_max;

  *lat_min = 90.0 - 360.0*atan(exp(-2 * M_PI * (-y_min))) / M_PI;
  *lat_max = 90.0 - 360.0*atan(exp(-2 * M_PI * (-y_max))) / M_PI;
}

void tile_center_scaled(double scale_x, double scale_y, double offset_x, double offset_y, uint64 quadint, int zoom, double* cx, double* cy) {
  unsigned int x, y;
  int zero_bits = MAX_ZOOM - zoom;
  quadint2xy(quadint, &x, &y);
  x >>= zero_bits;
  y >>= zero_bits;

  *cx = offset_x + ((x + x + 1) * 1.0 / (1ull << (zoom + 1))) * scale_x;
  *cy = offset_y + ((y + y + 1) * 1.0 / (1ull << (zoom + 1))) * scale_y;
}

void tile_center_webmercator(uint64 quadint, int zoom, double* cx, double* cy) {
  tile_center_scaled(WM_RANGE, -WM_RANGE, -WM_MAX, WM_MAX, quadint, zoom, cx, cy);
}

void tile_center(uint64 quadint, int zoom, double* lon, double* lat) {
  double x, y;
  tile_center_scaled(1.0, 1.0, -0.5, -0.5, quadint, zoom, &x, &y);

  *lon = 360.0 * x;
  *lat = 90.0 - 360.0*atan(exp(-2 * M_PI * (-y))) / M_PI;
}

void tile2range(uint64 quadint, int zoom, uint64* q_min, uint64* q_max)
{
    *q_min = quadint & tile_prefix_mask(zoom);
    *q_max = quadint | tile_suffix_mask(zoom);
}

void tile_children(uint64 quadint, int zoom, uint64* q_sw, uint64* q_nw, uint64* q_se, uint64* q_ne)
{
    int bit = ((MAX_ZOOM - zoom) << 1);
    *q_sw = quadint & tile_prefix_mask(zoom);
    *q_nw = *q_sw | (1ull << (bit - 2));
    *q_se = *q_sw | (1ull << (bit - 1));
    *q_ne = *q_se | (1ull << (bit - 2));
}

uint64 xyz2quadint(uint32 x, uint32 y, int zoom) {
    int bits = MAX_ZOOM - zoom;
    return xy2quadint(x << bits, y << bits);
}

void tile2xy(uint64 quadint, int zoom, uint32* x, uint32* y) {
  uint32 qx, qy;
  int bits = MAX_ZOOM - zoom;
  quadint2xy(quadint, &qx, &qy);
  *x = qx >> bits;
  *y = qy >> bits;
}

double box_area(double xmin, double ymin, double xmax, double ymax) {
    double w = xmax - xmin;
    double h = ymax - ymin;
    return (w > 0) && (h > 0) ? w*h : 0.0;
}

double box_intersection_area(double xmin1, double ymin1, double xmax1, double ymax1, double xmin2, double ymin2, double xmax2, double ymax2) {
    return box_area(MAX(xmin1, xmin2), MAX(ymin1, ymin2), MIN(xmax1, xmax2), MIN(ymax1, ymax2));
}

static void append_tile(PyObject* list, uint64 quadint, int zoom) {
    PyList_Append(list, Py_BuildValue("Ki", quadint, zoom));
}

void tiles_intersecting_webmercator_box(PyObject* result, double xmin, double ymin, double xmax, double ymax, uint64 quadint, int zoom, int max_zoom) {
    double tile_xmin, tile_ymin, tile_xmax, tile_ymax;
    double int_area;
    double tile_area;
    double area_tol = 42.0 / (1ull << (max_zoom + 1));
    tile2bbox_webmercator(quadint, zoom, &tile_xmin, &tile_ymin, &tile_xmax, &tile_ymax);

    int_area = box_intersection_area(xmin, ymin, xmax, ymax, tile_xmin, tile_ymin, tile_xmax, tile_ymax);
    if (int_area > 0) {
        tile_area = box_area(tile_xmin, tile_ymin, tile_xmax, tile_ymax);
        if (int_area - tile_area >= -area_tol) {
            /* the box contains the tile; add the tile to the results */
            append_tile(result, quadint, zoom);
        } else if (zoom == max_zoom) {
            if (int_area >= 0.5*tile_area) {
                /* large intersection; add the tile to the results */
                append_tile(result, quadint, zoom);
            } else {
                /* small intersection */
            }
        } else {
            /* Drill down to next level */
            uint64 child_sw, child_nw, child_se, child_ne;
            tile_children(quadint, zoom, &child_sw, &child_nw, &child_se, &child_ne);
            tiles_intersecting_webmercator_box(result, xmin, ymin, xmax, ymax, child_sw, zoom + 1, max_zoom);
            tiles_intersecting_webmercator_box(result, xmin, ymin, xmax, ymax, child_nw, zoom + 1, max_zoom);
            tiles_intersecting_webmercator_box(result, xmin, ymin, xmax, ymax, child_se, zoom + 1, max_zoom);
            tiles_intersecting_webmercator_box(result, xmin, ymin, xmax, ymax, child_ne, zoom + 1, max_zoom);
        }
    } else {
        /* No intersection */
    }
}

static PyObject* tiles_intersecting_webmercator_box_py(PyObject* self, PyObject* args)
{
    double xmin, ymin, xmax, ymax;
    int max_zoom;

    if (!PyArg_ParseTuple(args, "ddddi", &xmin, &ymin, &xmax, &ymax, &max_zoom))
        return NULL;

    PyObject* result = PyList_New(0);
    tiles_intersecting_webmercator_box(result, xmin, ymin, xmax, ymax, 0, 0, max_zoom);
    return result;
}

/*
 Input:  integer x y coordinates based on WebMercator in the range [0,2^31)
 Output: 62-bit quadkey value
*/
static PyObject*
xy2quadint_py(PyObject* self, PyObject* args)
{
    unsigned int x, y;

    if (!PyArg_ParseTuple(args, "ii", &x, &y))
        return NULL;

    return Py_BuildValue("K", xy2quadint(x, y));
}

/*
 Input:  integer x y coordinates based on WebMercator in the range [0,2^31)
 Output: web mercator coordinates (SRID 3857)
*/
static PyObject*
xy2webmercator_py(PyObject* self, PyObject* args)
{
    unsigned int x, y;
    double wm_x, wm_y;

    if (!PyArg_ParseTuple(args, "ii", &x, &y))
        return NULL;

    xy2webmercator(x, y, &wm_x, &wm_y);

    return Py_BuildValue("dd", wm_x, wm_y);
}

/*
 Input: web mercator coordinates (SRID 3857)
 Output:  integer x y coordinates based on WebMercator in the range [0,2^31)
*/
static PyObject*
webmercator2xy_py(PyObject* self, PyObject* args)
{
    unsigned int x, y;
    double wm_x, wm_y;

    if (!PyArg_ParseTuple(args, "dd", &wm_x, &wm_y))
        return NULL;

    webmercator2xy(wm_x, wm_y, &x, &y);

    return Py_BuildValue("ii", x, y);
}

/*
 Input: longitude, latitude in WGS84 (SRID 4326) coordinates in degrees
 Output: 62-bit quadkey value
*/
static PyObject*
lonlat2quadint_py(PyObject* self, PyObject* args)
{
    double lon, lat;

    if (!PyArg_ParseTuple(args, "dd", &lon, &lat))
        return NULL;

    return Py_BuildValue("K", lonlat2quadint(lon, lat));
}

/*
 Input: longitude, latitude in WGS84 (SRID 4326) coordinates in degrees
 Output: 62-bit quadkey value, x, y (in mercator projection)
*/
static PyObject*
lonlat2quadintxy_py(PyObject* self, PyObject* args)
{
    double lon, lat;
    uint32 x, y;

    if (!PyArg_ParseTuple(args, "dd", &lon, &lat))
        return NULL;

    lonlat2xy(lon, lat, MAX_ZOOM, &x, &y);

    return Py_BuildValue("KII", xy2quadint(x, y), x, y);
}


/*
 Input:  web mercator coordinates (SRID 3857)
 Output: 62-bit quadkey value
*/
static PyObject*
webmercator2quadint_py(PyObject* self, PyObject* args)
{
    double wm_x, wm_y;
    unsigned int x, y;

    if (!PyArg_ParseTuple(args, "dd", &wm_x, &wm_y))
        return NULL;

    webmercator2xy(wm_x, wm_y, &x, &y);
    return Py_BuildValue("K", xy2quadint(x, y));
}

/*
 Input: 62-bit quadkey value
 Output:  web mercator bounding box coordinates (SRID 3857)
*/
static PyObject*
tile2bbox_webmercator_py(PyObject* self, PyObject* args)
{
    uint64 quadint;
    int zoom;
    double x_min, y_min, x_max, y_max;

    if (!PyArg_ParseTuple(args, "Ki", &quadint, &zoom))
        return NULL;

    tile2bbox_webmercator(quadint, zoom, &x_min, &y_min, &x_max, &y_max);

    return Py_BuildValue("dddd", x_min, y_min, x_max, y_max);
}

/*
 Input: 62-bit quadkey value
 Output: WGS84  bounding box coordinates (SRID 4326)
*/
static PyObject*
tile2bbox_py(PyObject* self, PyObject* args)
{
    uint64 quadint;
    int zoom;
    double x_min, y_min, x_max, y_max;

    if (!PyArg_ParseTuple(args, "Ki", &quadint, &zoom))
        return NULL;

    tile2bbox(quadint, zoom, &x_min, &y_min, &x_max, &y_max);

    return Py_BuildValue("dddd", x_min, y_min, x_max, y_max);
}

static PyObject*
tile2range_py(PyObject* self, PyObject* args)
{
    uint64 quadint;
    int zoom;
    uint64 q_min, q_max;

    if (!PyArg_ParseTuple(args, "Ki", &quadint, &zoom))
        return NULL;

    tile2range(quadint, zoom, &q_min, &q_max);

    return Py_BuildValue("KK", q_min, q_max);
}

static PyObject*
tile_mask_py(PyObject* self, PyObject* args)
{
    int zoom;

    if (!PyArg_ParseTuple(args, "i", &zoom))
        return NULL;

    return Py_BuildValue("K", tile_prefix_mask(zoom));
}

static PyObject*
tile_center_webmercator_py(PyObject* self, PyObject* args)
{
    uint64 quadint;
    int zoom;
    double x, y;

    if (!PyArg_ParseTuple(args, "Ki", &quadint, &zoom))
        return NULL;

    tile_center_webmercator(quadint, zoom, &x, &y);

    return Py_BuildValue("dd", x, y);
}

static PyObject*
tile_center_py(PyObject* self, PyObject* args)
{
    uint64 quadint;
    int zoom;
    double x, y;

    if (!PyArg_ParseTuple(args, "Ki", &quadint, &zoom))
        return NULL;

    tile_center(quadint, zoom, &x, &y);

    return Py_BuildValue("dd", x, y);
}

static PyObject*
tile_children_py(PyObject* self, PyObject* args)
{
    uint64 quadint;
    int zoom;
    uint64 q_sw, q_nw, q_se, q_ne;

    if (!PyArg_ParseTuple(args, "Ki", &quadint, &zoom))
        return NULL;

    if (zoom < MAX_ZOOM) {
         tile_children(quadint, zoom, &q_sw, &q_nw, &q_se, &q_ne);
         return Py_BuildValue("KKKK", q_sw, q_nw, q_se, q_ne);
    } else {
         return Py_BuildValue("");
    }
}

static PyObject*
xyz2quadint_py(PyObject* self, PyObject* args)
{
    uint32 x, y;
    int zoom;

    if (!PyArg_ParseTuple(args, "IIi", &x, &y, &zoom))
        return NULL;

    return Py_BuildValue("K", xyz2quadint(x, y, zoom));
}

static PyObject*
tile2xyz_py(PyObject* self, PyObject* args)
{
    uint64 quadint;
    int zoom;
    uint32 x, y;

    if (!PyArg_ParseTuple(args, "Ki", &quadint, &zoom))
        return NULL;

    tile2xy(quadint, zoom, &x, &y);
    return Py_BuildValue("IIi", x, y, zoom);
}

static PyObject*
lonlat2xy_py(PyObject* self, PyObject* args)
{
    double lat, lon;
    uint32 x, y;

    if (!PyArg_ParseTuple(args, "dd", &lon, &lat))
        return NULL;

    lonlat2xy(lon, lat, MAX_ZOOM, &x, &y);
    return Py_BuildValue("II", x, y);
}

static PyMethodDef QuadkeyMethods[] =
{
     {"xy2quadint", xy2quadint_py, METH_VARARGS, "xy2quadint_py"},
     {"lonlat2quadint", lonlat2quadint_py, METH_VARARGS, "lonlat2quadint"},
     {"xy2webmercator", xy2webmercator_py, METH_VARARGS, "xy2webmercator"},
     {"webmercator2xy", webmercator2xy_py, METH_VARARGS, "webmercator2xy"},
     {"webmercator2quadint", webmercator2quadint_py, METH_VARARGS, "webmercator2quadint"},
     {"tile2bbox_webmercator", tile2bbox_webmercator_py, METH_VARARGS, "tile2bbox_webmercator"},
     {"tile2bbox", tile2bbox_py, METH_VARARGS, "tile2bbox"},
     {"tile2range", tile2range_py, METH_VARARGS, "tile2range"},
     {"tile_mask", tile_mask_py, METH_VARARGS, "tile_mask"},
     {"tile_center_webmercator", tile_center_webmercator_py, METH_VARARGS, "tile_center_webmercator"},
     {"tile_center", tile_center_py, METH_VARARGS, "tile_center"},
     {"tile_children", tile_children_py, METH_VARARGS, "tile_children"},
     {"xyz2quadint", xyz2quadint_py, METH_VARARGS, "xyz2quadint"},
     {"tile2xyz", tile2xyz_py, METH_VARARGS, "tile2xyz"},
     {"tiles_intersecting_webmercator_box", tiles_intersecting_webmercator_box_py, METH_VARARGS, "tiles_intersecting_webmercator_box"},
     {"lonlat2xy", lonlat2xy_py, METH_VARARGS, "lonlat2xy"},
     {"lonlat2quadintxy", lonlat2quadintxy_py, METH_VARARGS, "lonlat2quadintxy"},
     {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC
initquadkey(void)
{
     (void) Py_InitModule("quadkey", QuadkeyMethods);
}
