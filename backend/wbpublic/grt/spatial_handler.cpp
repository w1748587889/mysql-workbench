/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include "spatial_handler.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "base/log.h"

DEFAULT_LOG_DOMAIN("spatial");

#ifdef _WIN32
static void __stdcall ogr_error_handler(CPLErr eErrClass, int err_no, const char *msg)
{
  log_error("gdal error: %d, %s\n", err_no, msg);
}
#else
static void ogr_error_handler(CPLErr eErrClass, int err_no, const char *msg)
{
  log_error("gdal error: %d, %s\n", err_no, msg);
}
#endif

bool spatial::operator== (ProjectionView &v1, ProjectionView &v2)
{
  return (v1.MaxLat == v2.MaxLat &&
      v1.MaxLon == v2.MaxLon &&
      v1.MinLat == v2.MinLat &&
      v1.MinLon == v2.MinLon &&
      v1.height == v2.height &&
      v1.width == v2.width);
}
bool spatial::operator!= (ProjectionView &v1, ProjectionView &v2)
{
  return !(v1==v2);
}

bool spatial::operator== (Envelope &env1, Envelope &env2)
{
  return env1.bottom_right == env2.bottom_right && env1.top_left == env2.top_left;
}

bool spatial::operator!= (Envelope &env1, Envelope &env2)
{
  return !(env1==env2);
}

spatial::Envelope::Envelope() : converted(false)
{
  top_left.x = 180;
  top_left.y = -90;
  bottom_right.x = -180;
  bottom_right.y = 90;
}

spatial::Envelope::Envelope(double left, double top, double right, double bottom) : converted(false)
{
  top_left.x = left;
  top_left.y = top;
  bottom_right.x = right;
  bottom_right.y = bottom;
  top_left.x = 180;
  top_left.y = -90;
  bottom_right.x = -180;
  bottom_right.y = 90;
}

bool spatial::Envelope::is_init()
{
  return (top_left.x != 180 && top_left.y != -90 && bottom_right.x != -180 && bottom_right.y != 90);
}

bool spatial::ShapeContainer::within(const base::Point &p) const
{
  switch(type)
  {
  case ShapePoint:
    return within_point(p);
  case ShapeLineString:
    return within_line(points, p);
  case ShapeLinearRing:
    return within_linearring(p);
  case ShapePolygon:
    return within_polygon(p);
  default:
    return false;
  }
}

bool spatial::ShapeContainer::within_linearring(const base::Point &p) const
{
  if (points.empty())
    return false;
  std::vector<base::Point> tmp = points;
  tmp.push_back(*tmp.begin());

  return within_line(tmp, p);
}

//XXX see if all this code can be replaced with boost
static double distance_to_segment(const base::Point &start, const base::Point &end, const base::Point &p)
{
  double dx = end.x - start.x;
  double dy = end.y - start.y;
  if (dx == 0 && dy == 0)
    return sqrt(pow(p.x - start.x, 2) + pow(p.y - start.y, 2));

  double dist_to_line = ((p.x - start.x) * dx + (p.y - start.y) * dy) / (pow(dx, 2) + pow(dy, 2));

  if (dist_to_line > 1)
  {
    dx = p.x - end.x;
    dy = p.y - end.y;
  }
  else if (dist_to_line < 0)
  {
    dx = p.x - start.x;
    dy = p.y - start.y;
  }
  else
  {
    dx = p.x - (start.x + dist_to_line * dx);
    dy = p.y - (start.y + dist_to_line * dy);
  }
  return sqrt(pow(dx, 2) + pow(dy, 2));
}

bool spatial::ShapeContainer::within_line(const std::vector<base::Point> &point_list, const base::Point &p) const
{
  if (point_list.empty())
    return false;

  std::vector<base::Point>::const_iterator it = point_list.begin(), it_tmp = point_list.begin();
  while (++it != point_list.end())
  {
    try
    {
      if (distance_to_segment(*it_tmp, *it, p) <= 1.0)
        return true;
    }
    catch (std::logic_error &e)
    {
      //distance can raise Divide by zero exception, we silently skip this
    }

    it_tmp = it;
  }

  return false;
}

bool spatial::ShapeContainer::within_polygon(const base::Point &p) const
{
  if (points.empty())
      return false;

  //first we check if we're in the bounding box cause maybe it's pointless to check all the polygon points
  if (!(bounding_box.top_left.x <= p.x && bounding_box.top_left.y <= p.y && bounding_box.bottom_right.x >= p.x && bounding_box.bottom_right.y >= p.y))
    return false;

  bool c = false;
  int i, j = 0;
  int nvert = points.size();
  for (i = 0, j = nvert-1; i < nvert; j = i++) {
    if ( ((points[i].y > p.y) != (points[j].y > p.y)) && (p.x < (points[j].x - points[i].x) * (p.y - points[i].y) / (points[j].y - points[i].y) + points[i].x) )
      c = !c;
  }
  return c;
}

bool spatial::ShapeContainer::within_point(const base::Point &p) const
{
  if (points.empty())
    return false;

  double rval = sqrt(pow((p.x - points[0].x), 2) + pow((p.y - points[0].y), 2));
  if (rval < 4.0) //4 is tolerance for point to point distance
  {
    return true;
  }
  return false;
}

spatial::ShapeContainer::ShapeContainer()
{

}

std::string spatial::shape_description(ShapeType shp)
{
  switch(shp)
  {
  case ShapePolygon:
    return "Polygon";
  case ShapeLinearRing:
    return "LinearRing";
  case ShapeLineString:
    return "LineString";
  case ShapePoint:
    return "Point";
  case ShapeUnknown:
  default:
    return "Unknown shape type";

  }
  return "";
}

spatial::Projection::Projection()
{
  char* m_wkt = const_cast<char*>("PROJCS[\"World_Mercator\", "
      "GEOGCS[\"GCS_WGS_1984\", "
      "DATUM[\"WGS_1984\", "
      "SPHEROID[\"WGS_1984\",6378137,298.257223563]], "
      "PRIMEM[\"Greenwich\",0], "
      "UNIT[\"Degree\",0.017453292519943295]], "
      "PROJECTION[\"Mercator_1SP\"], "
      "PARAMETER[\"False_Easting\",0], "
      "PARAMETER[\"False_Northing\",0], "
      "PARAMETER[\"Central_Meridian\",0], "
      "PARAMETER[\"Standard_Parallel_1\",0], "
      "UNIT[\"Meter\",1], "
      "AUTHORITY[\"EPSG\",\"54004\"]]");
  _mercator_srs.importFromWkt(&m_wkt);

  char* e_wkt = const_cast<char*>("PROJCS[\"World_Equidistant_Cylindrical\","
      "GEOGCS[\"GCS_WGS_1984\","
      "DATUM[\"WGS_1984\","
      "SPHEROID[\"WGS_1984\",6378137,298.257223563]],"
      "PRIMEM[\"Greenwich\",0],"
      "UNIT[\"Degree\",0.017453292519943295]],"
      "PROJECTION[\"Equirectangular\"],"
      "PARAMETER[\"False_Easting\",0],"
      "PARAMETER[\"False_Northing\",0],"
      "PARAMETER[\"Central_Meridian\",0],"
      "PARAMETER[\"Standard_Parallel_1\",60],"
      "UNIT[\"Meter\",1],"
      "AUTHORITY[\"EPSG\",\"54002\"]]");
  _equirectangular_srs.importFromWkt(&e_wkt);

  char* r_wkt = const_cast<char*>("PROJCS[\"World_Robinson\","
    "GEOGCS[\"GCS_WGS_1984\","
    "DATUM[\"WGS_1984\","
    "SPHEROID[\"WGS_1984\",6378137,298.257223563]],"
    "PRIMEM[\"Greenwich\",0],"
    "UNIT[\"Degree\",0.017453292519943295]],"
    "PROJECTION[\"Robinson\"],"
    "PARAMETER[\"False_Easting\",0],"
    "PARAMETER[\"False_Northing\",0],"
    "PARAMETER[\"Central_Meridian\",0],"
    "UNIT[\"Meter\",1],"
    "AUTHORITY[\"EPSG\",\"54030\"]]");
  _robinson_srs.importFromWkt(&r_wkt);

  char *g_wkt = const_cast<char*>("GEOGCS[\"WGS 84\", "
      "DATUM[\"WGS_1984\", "
      "SPHEROID[\"WGS 84\",6378137,298.257223563, "
      "AUTHORITY[\"EPSG\",\"7030\"]], "
          "AUTHORITY[\"EPSG\",\"6326\"]], "
          "PRIMEM[\"Greenwich\",0, "
          "AUTHORITY[\"EPSG\",\"8901\"]], "
          "UNIT[\"degree\",0.01745329251994328, "
          "AUTHORITY[\"EPSG\",\"9122\"]], "
          "AUTHORITY[\"EPSG\",\"4326\"]]");
  _geodetic_srs.importFromWkt(&g_wkt);

  char *b_wkt = const_cast<char*>("PROJCS[\"World_Bonne\", "
      "GEOGCS[\"GCS_WGS_1984\", "
      "DATUM[\"WGS_1984\", "
      "SPHEROID[\"WGS_1984\",6378137,298.257223563]], "
      "PRIMEM[\"Greenwich\",0], "
      "UNIT[\"Degree\",0.017453292519943295]], "
      "PROJECTION[\"Bonne\"], "
      "PARAMETER[\"False_Easting\",0], "
      "PARAMETER[\"False_Northing\",0], "
      "PARAMETER[\"Central_Meridian\",0], "
      "PARAMETER[\"Standard_Parallel_1\",60], "
      "UNIT[\"Meter\",1], "
      "AUTHORITY[\"EPSG\",\"54024\"]]");
  _bonne_srs.importFromWkt(&b_wkt);

}

spatial::Projection& spatial::Projection::get_instance()
{
  static Projection instance;
  return instance;
}

OGRSpatialReference* spatial::Projection::get_projection(ProjectionType type)
{
  switch(type)
  {
  case ProjMercator:
    return &_mercator_srs;
  case ProjEquirectangular:
    return &_equirectangular_srs;
  case ProjBonne:
    return &_bonne_srs;
  case ProjRobinson:
    return &_robinson_srs;
  case ProjGeodetic:
    return &_geodetic_srs;
  default:
    throw std::logic_error("Specified projection type is unsupported\n");
  }
}

void spatial::Importer::extract_points(OGRGeometry *shape, std::deque<ShapeContainer> &shapes_container)
{
  OGRwkbGeometryType flat_type = wkbFlatten(shape->getGeometryType());

  if (flat_type == wkbPoint)
  {
    ShapeContainer container;
    container.type = ShapePoint;
    OGRPoint *point = (OGRPoint*)shape;
    container.points.push_back(base::Point(point->getX(), point->getY()));
    container.bounding_box.top_left = container.bounding_box.bottom_right = base::Point(point->getX(), point->getY());
    shapes_container.push_back(container);

  }
  else if (flat_type == wkbLineString)
  {
    ShapeContainer container;
    container.type = ShapeLineString;
    OGRLineString *line = (OGRLineString*) shape;
    OGREnvelope env;
    line->getEnvelope(&env);
    container.bounding_box.top_left.x = env.MinX;
    container.bounding_box.top_left.y = env.MaxY;
    container.bounding_box.bottom_right.x = env.MaxX;
    container.bounding_box.bottom_right.y = env.MinY;
    int nPoints = line->getNumPoints();

    container.points.reserve(nPoints);
    for (int i = nPoints - 1; i >= 0 && !_interrupt; i--)
      container.points.push_back(base::Point(line->getX(i), line->getY(i)));

    shapes_container.push_back(container);

  }
  else if (flat_type == wkbLinearRing)
  {
    ShapeContainer container;
    container.type = ShapeLinearRing;
    OGRLinearRing *ring = (OGRLinearRing *) shape;
    OGREnvelope env;
    ring->getEnvelope(&env);
    container.bounding_box.top_left.x = env.MinX;
    container.bounding_box.top_left.y = env.MaxY;
    container.bounding_box.bottom_right.x = env.MaxX;
    container.bounding_box.bottom_right.y = env.MinY;
    int nPoints = ring->getNumPoints();
    container.points.reserve(nPoints);
    for (int i = nPoints - 1; i >= 0 && !_interrupt; i--)
      container.points.push_back(base::Point(ring->getX(i), ring->getY(i)));

    shapes_container.push_back(container);

  }
  else if (flat_type == wkbPolygon)
  {
    ShapeContainer container;
    container.type = ShapePolygon;

    OGRPolygon *poly = (OGRPolygon*) shape;
    OGRLinearRing *ring = poly->getExteriorRing();
    int nPoints = ring->getNumPoints();
    container.points.reserve(nPoints);
    OGREnvelope env;
    ring->getEnvelope(&env);
    container.bounding_box.top_left.x = env.MinX;
    container.bounding_box.top_left.y = env.MaxY;
    container.bounding_box.bottom_right.x = env.MaxX;
    container.bounding_box.bottom_right.y = env.MinY;

    for (int i = nPoints - 1; i >= 0; i--)
      container.points.push_back(base::Point(ring->getX(i), ring->getY(i)));
    shapes_container.push_back(container);

    for (int i = 0; i < poly->getNumInteriorRings() && !_interrupt; ++i)
      extract_points(poly->getInteriorRing(i), shapes_container);

  }
  else if (flat_type == wkbMultiPoint || flat_type == wkbMultiLineString
      || flat_type == wkbMultiPolygon || flat_type == wkbGeometryCollection)
  {
    OGRGeometryCollection *geoCollection = (OGRGeometryCollection*) shape;
    for (int i = 0; i < geoCollection->getNumGeometries() && !_interrupt; ++i)
      extract_points(geoCollection->getGeometryRef(i), shapes_container);
  }
}

void spatial::Importer::get_points(std::deque<ShapeContainer> &shapes_container)
{
  if (_geometry)
    extract_points(_geometry, shapes_container);
}

void spatial::Importer::get_envelope(spatial::Envelope &env)
{
  if (_geometry)
  {
    OGREnvelope ogr_env;
    _geometry->getEnvelope(&ogr_env);
    env.top_left.x = ogr_env.MinX;
    env.top_left.y = ogr_env.MaxY;
    env.bottom_right.x = ogr_env.MaxX;
    env.bottom_right.y = ogr_env.MinY;
  }
}

spatial::Importer::Importer() : _geometry(NULL), _interrupt(false)
{
}

OGRGeometry *spatial::Importer::steal_data()
{
  OGRGeometry *tmp = _geometry;
  _geometry = NULL;
  return tmp;
}

int spatial::Importer::import_from_mysql(const std::string &data)
{
  OGRErr ret_val = OGRGeometryFactory::createFromWkb((unsigned char*)const_cast<char*>(&(*(data.begin()+4))), NULL, &_geometry);

  if (_geometry)
    _geometry->assignSpatialReference(Projection::get_instance().get_projection(ProjGeodetic));

  if (ret_val == OGRERR_NONE)
    return 0;
  else
    return 1;
}

int spatial::Importer::import_from_wkt(std::string data)
{
  char *d = &(*data.begin());
  OGRErr ret_val = OGRGeometryFactory::createFromWkt(&d, NULL, &_geometry);

  if (_geometry)
    _geometry->assignSpatialReference(Projection::get_instance().get_projection(ProjGeodetic));

  if (ret_val == OGRERR_NONE)
    return 0;
  else
    return 1;
}

std::string spatial::Importer::as_wkt()
{
  char *data;
  if (_geometry)
  {
    OGRErr err;
    if ((err = _geometry->exportToWkt(&data)) != OGRERR_NONE)
    {
      log_error("Error exporting data to WKT (%i)\n", err);
    }
    else
    {
      std::string tmp(data);
      OGRFree(data);
      return tmp;
    }
  }
  return "";
}

std::string spatial::Importer::as_kml()
{
  char *data;
  if (_geometry)
  {
    if (!(data = _geometry->exportToKML()))
    {
      log_error("Error exporting data to KML\n");
    }
    else
    {
      std::string tmp(data);
      CPLFree(data);
      return tmp;
    }
  }
  return "";
}

std::string spatial::Importer::as_json()
{
  char *data;
  if (_geometry)
  {
    if (!(data = _geometry->exportToJson()))
    {
      log_error("Error exporting data to JSON\n");
    }
    else
    {
      std::string tmp(data);
      CPLFree(data);
      return tmp;
    }
  }
  return "";
}

std::string spatial::Importer::as_gml()
{
  char *data;
  if (_geometry)
  {
    if (!(data = _geometry->exportToGML()))
    {
      log_error("Error exporting data to GML\n");
    }
    else
    {
      std::string tmp(data);
      CPLFree(data);
      return tmp;
    }
  }
  return "";
}

void spatial::Importer::interrupt()
{
  _interrupt = true;
}

spatial::Converter::Converter(ProjectionView view, OGRSpatialReference *src_srs, OGRSpatialReference *dst_srs)
: _geo_to_proj(NULL), _proj_to_geo(NULL), _source_srs(NULL), _target_srs(NULL), _interrupt(false)
{
  CPLSetErrorHandler(&ogr_error_handler);
  OGRRegisterAll();
  change_projection(view, src_srs, dst_srs);
}

const char* spatial::Converter::dec_to_dms(double angle, AxisType axis, int precision)
{
  switch(axis)
  {
  case AxisLat:
    return GDALDecToDMS(angle, "Lat", precision);
  case AxisLon:
    return GDALDecToDMS(angle, "Long", precision);
  default:
    throw std::logic_error("Unknown axis type\n");
  }
}

spatial::Converter::~Converter()
{
  base::RecMutexLock mtx(_projection_protector);
}

void spatial::Converter::change_projection(OGRSpatialReference *src_srs, OGRSpatialReference *dst_srs)
{
  change_projection(_view, src_srs, dst_srs);
}

void spatial::Converter::change_projection(ProjectionView view, OGRSpatialReference *src_srs, OGRSpatialReference *dst_srs)
{
  base::RecMutexLock mtx(_projection_protector);
  int recalculate = 0;

  if (view != _view)
  {
    _view = view;
    recalculate = 1;
  }

  if (src_srs != NULL && src_srs != _source_srs)
  {
    _source_srs = src_srs;
    recalculate = 2;
  }

  if (dst_srs != NULL && dst_srs != _target_srs)
  {
    _target_srs = dst_srs;
    recalculate = 2;
  }

  if (recalculate == 0)
    return;

  if (recalculate == 2)
  {
    if (_geo_to_proj != NULL)
      OCTDestroyCoordinateTransformation(_geo_to_proj);
    if (_proj_to_geo != NULL)
      OCTDestroyCoordinateTransformation(_proj_to_geo);

    _geo_to_proj = OGRCreateCoordinateTransformation(_source_srs, _target_srs);
    _proj_to_geo = OGRCreateCoordinateTransformation(_target_srs, _source_srs);
    if (!_geo_to_proj || !_proj_to_geo)
      throw std::logic_error("Unable to perform specified transformation.\n");
  }

  double minLat = _view.MinLat, maxLon = _view.MaxLon, maxLat = _view.MaxLat, minLon = _view.MinLon;

  if (!_geo_to_proj->Transform(1, &minLat, &maxLon, 0))
  {
    char * proj4;
    _target_srs->exportToProj4(&proj4);
    log_error("Unable to perform requested reprojection from WGS84, to %s\n", proj4);
    CPLFree(proj4);
  }

  if (!_geo_to_proj->Transform(1, &maxLat, &minLon, 0))
  {
    char * proj4;
    _target_srs->exportToProj4(&proj4);
    log_error("Unable to perform requested reprojection from WGS84, to %s\n", proj4);
    CPLFree(proj4);
  }


  _adf_projection[0] = minLat;
  _adf_projection[1] = (maxLat - minLat) / (double)_view.width;
  _adf_projection[2] = 0;
  _adf_projection[3] = maxLon;
  _adf_projection[4] = 0;
  _adf_projection[5] = -(maxLon - minLon) / (double)_view.height;
  GDALInvGeoTransform(_adf_projection, _inv_projection);
}

void spatial::Converter::to_projected(int x, int y, double &lat, double &lon)
{
  base::RecMutexLock mtx(_projection_protector);
  lat = _adf_projection[3] + (double) x * _adf_projection[4] + (double) y * _adf_projection[5];
  lon = _adf_projection[0] + (double) x * _adf_projection[1] + (double) y * _adf_projection[2];
}

void spatial::Converter::from_projected(double lat, double lon, int &x, int &y)
{
  base::RecMutexLock mtx(_projection_protector);
  x = (int)(_inv_projection[0] + _inv_projection[1] * lat);
  y = (int)(_inv_projection[3] + _inv_projection[5] * lon);
}

bool spatial::Converter::to_latlon(int x, int y, double &lat, double &lon)
{
  to_projected(x, y, lat, lon);
  //for lat/lon projection, coordinate order is reversed and it's lon/lat
  return from_proj_to_latlon(lon, lat);
}

bool spatial::Converter::from_latlon(double lat, double lon, int &x, int &y)
{
  bool ret_val = from_latlon_to_proj(lon, lat);
  from_projected(lon, lat, x, y);
  return ret_val;
}

bool spatial::Converter::from_latlon_to_proj(double &lat, double &lon)
{
  base::RecMutexLock mtx(_projection_protector);
  return _geo_to_proj->Transform(1, &lat, &lon) != 0;
}

bool spatial::Converter::from_proj_to_latlon(double &lat, double &lon)
{
  base::RecMutexLock mtx(_projection_protector);
  return _proj_to_geo->Transform(1, &lat, &lon) != 0;
}

void spatial::Converter::transform_points(std::deque<ShapeContainer> &shapes_container)
{

  std::deque<ShapeContainer>::iterator it;
  for(it = shapes_container.begin(); it != shapes_container.end() && !_interrupt; it++)
  {
    std::deque<size_t> for_removal;
    for (size_t i = 0; i < (*it).points.size() && !_interrupt; i++)
    {
      if(!_geo_to_proj->Transform(1, &(*it).points[i].x, &(*it).points[i].y))
        for_removal.push_back(i);
    }

    if(_geo_to_proj->Transform(1, &(*it).bounding_box.bottom_right.x, &(*it).bounding_box.bottom_right.y) &&
       _geo_to_proj->Transform(1, &(*it).bounding_box.top_left.x, &(*it).bounding_box.top_left.y))
    {
      int x, y;
      from_projected((*it).bounding_box.bottom_right.x, (*it).bounding_box.bottom_right.y, x, y);
      (*it).bounding_box.bottom_right.x = x;
      (*it).bounding_box.bottom_right.y = y;
      from_projected((*it).bounding_box.top_left.x, (*it).bounding_box.top_left.y, x, y);
      (*it).bounding_box.top_left.x = x;
      (*it).bounding_box.top_left.y = y;
      (*it).bounding_box.converted = true;
    }

    if (!for_removal.empty())
      log_debug("%i points that could not be converted were skipped\n", (int)for_removal.size());

    std::deque<size_t>::reverse_iterator rit;
    for (rit = for_removal.rbegin(); rit != for_removal.rend() && !_interrupt; rit++)
      (*it).points.erase((*it).points.begin() + *rit);

    for(size_t i=0; i < (*it).points.size() && !_interrupt; i++)
    {
      int x, y;
      from_projected((*it).points[i].x, (*it).points[i].y, x, y);
      (*it).points[i].x = x;
      (*it).points[i].y = y;
    }
  }
}

void spatial::Converter::interrupt()
{
  _interrupt = true;
}

using namespace spatial;

Feature::Feature(Layer *layer, int row_id, const std::string &data, bool wkt = false)
: _owner(layer), _row_id(row_id)
{
  if (wkt)
    _geometry.import_from_wkt(data);
  else
    _geometry.import_from_mysql(data);
}

Feature::~Feature()
{
}

void Feature::get_envelope(spatial::Envelope &env)
{
  _geometry.get_envelope(env);
}

void Feature::render(Converter *converter)
{
  std::deque<ShapeContainer> tmp_shapes;
  _geometry.get_points(tmp_shapes);
  converter->transform_points(tmp_shapes);
  _shapes = tmp_shapes;
}


bool Feature::within(const base::Point &p)
{
  for (std::deque<ShapeContainer>::const_iterator it = _shapes.begin(); it != _shapes.end() && !_owner->_interrupt; it++)
  {
    if ((*it).within(p))
      return true;
  }
  return false;
}

void Feature::interrupt()
{
  _geometry.interrupt();
}

void Feature::repaint(mdc::CairoCtx &cr, float scale, const base::Rect &clip_area, bool fill_polygons)
{
  for (std::deque<ShapeContainer>::iterator it = _shapes.begin(); it != _shapes.end() && !_owner->_interrupt; it++)
  {
    if ((*it).points.empty())
    {
      log_error("%s is empty", shape_description(it->type).c_str());
      continue;
    }

    switch (it->type)
    {
      case ShapePolygon:
        cr.new_path();
        cr.move_to((*it).points[0]);
        for (size_t i = 1; i < (*it).points.size(); i++)
          cr.line_to((*it).points[i]);
        cr.close_path();
        if (fill_polygons)
          cr.fill();
        cr.stroke();
        break;

      case ShapeLineString:
        cr.move_to((*it).points[0]);
        for (size_t i = 1; i < (*it).points.size(); i++)
          cr.line_to((*it).points[i]);
        cr.stroke();
        break;

      case ShapePoint:
        cr.save();
        // for points, we paint the marker at the exact position but reverse the scaling, so that the marker size is constant
        cr.translate((*it).points[0]);
        cr.scale(1.0/scale, 1.0/scale);
        cr.rectangle(-5, -5, 5, 5);
        cr.fill();
        cr.restore();
        break;

      default:
        log_debug("Unknown type %i\n", it->type);
        break;
    }
  }
  cr.check_state();
}


static void extend_env(spatial::Envelope &env, const spatial::Envelope &env2)
{
  env.top_left.x = MIN(env.top_left.x, env2.top_left.x);
  env.top_left.y = MAX(env.top_left.y, env2.top_left.y);
  env.bottom_right.x = MAX(env.bottom_right.x, env2.bottom_right.x);
  env.bottom_right.y = MIN(env.bottom_right.y, env2.bottom_right.y);
}


Layer::Layer(int layer_id, base::Color color)
: _layer_id(layer_id), _color(color), _show(false), _interrupt(false)
{
  _spatial_envelope.top_left.x = 180;
  _spatial_envelope.top_left.y = -90;
  _spatial_envelope.bottom_right.x = -180;
  _spatial_envelope.bottom_right.y = 90;
  _fill_polygons = false;
}

Layer::~Layer()
{
  for (std::deque<Feature*>::iterator it = _features.begin(); it != _features.end(); ++it)
    delete *it;
}

void Layer::set_fill_polygons(bool fill)
{
  _fill_polygons = fill;
}

bool Layer::get_fill_polygons()
{
  return _fill_polygons;
}

void Layer::interrupt()
{
  _interrupt = true;
   for (std::deque<Feature*>::iterator it = _features.begin(); it != _features.end(); ++it)
     (*it)->interrupt();
}

bool Layer::hidden()
{
  return !_show;
}

int Layer::layer_id()
{
  return _layer_id;
}

void Layer::set_show(bool flag)
{
  _show = flag;
  if (flag)
    load_data();
}

void Layer::add_feature(int row_id, const std::string &geom_data, bool wkt)
{
  spatial::Envelope env;
  Feature *feature = new Feature(this, row_id, geom_data, wkt);
  feature->get_envelope(env);
  extend_env(_spatial_envelope, env);
  _features.push_back(feature);
}

void Layer::repaint(mdc::CairoCtx &cr, float scale, const base::Rect &clip_area)
{
  std::deque<ShapeContainer>::const_iterator it;

  cr.save();
  cr.set_line_width(0.5);
  cr.set_color(_color);
  for (std::deque<Feature*>::iterator it = _features.begin(); it != _features.end() && !_interrupt; ++it)
    (*it)->repaint(cr, scale, clip_area, _fill_polygons);

  cr.restore();
}


float Layer::query_render_progress()
{
  return _render_progress;
}

spatial::Envelope spatial::Layer::get_envelope()
{
  return _spatial_envelope;
}


void Layer::render(Converter *converter)
{
  _render_progress = 0.0;
  float step = 1.0f / _features.size();

  for (std::deque<spatial::Feature*>::iterator iter = _features.begin(); iter != _features.end() && !_interrupt; ++iter)
  {
    (*iter)->render(converter);
    _render_progress += step;
  }
}

spatial::Feature* Layer::feature_within(const base::Point &p)
{
  for (std::deque<spatial::Feature*>::iterator iter = _features.begin(); iter != _features.end() && !_interrupt; ++iter)
  {
    if ((*iter)->within(p))
      return *iter;
  }
  return NULL;
}
