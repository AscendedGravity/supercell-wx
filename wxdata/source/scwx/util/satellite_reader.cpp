#include <scwx/util/satellite_reader.hpp>
#include <scwx/util/logger.hpp>
#include <cmath>
#include <algorithm>
#include <netcdf.h>
#include <netcdf_mem.h>

namespace scwx
{
namespace util
{

static const std::string logPrefix_ = "scwx::util::satellite_reader";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

#ifndef M_PI
#   define M_PI 3.14159265358979323846
#endif

std::optional<SatelliteData> SatelliteReader::ReadMem(const std::string& data,
                                                      bool isInfrared)
{
   if (data.empty())
   {
      logger_->error("Empty data stream passed to SatelliteReader");
      return std::nullopt;
   }

   int ncid = 0;
   int status =
      nc_open_mem("satellite_data",
                  NC_NOWRITE,
                  data.size(),
                  const_cast<void*>(static_cast<const void*>(data.data())),
                  &ncid);
   if (status != NC_NOERR)
   {
      logger_->error("nc_open_mem failed with error: {}", status);
      return std::nullopt;
   }

   // Query variable IDs
   int x_varid    = 0;
   int y_varid    = 0;
   int proj_varid = 0;
   int cmip_varid = 0;

   bool query_ok = true;
   if (nc_inq_varid(ncid, "x", &x_varid) != NC_NOERR)
   {
      logger_->error("Failed to query 'x' variable ID");
      query_ok = false;
   }
   if (nc_inq_varid(ncid, "y", &y_varid) != NC_NOERR)
   {
      logger_->error("Failed to query 'y' variable ID");
      query_ok = false;
   }
   if (nc_inq_varid(ncid, "goes_imager_projection", &proj_varid) != NC_NOERR)
   {
      logger_->error("Failed to query 'goes_imager_projection' variable ID");
      query_ok = false;
   }

   if (nc_inq_varid(ncid, "CMI", &cmip_varid) != NC_NOERR)
   {
      logger_->info("'CMI' variable not found, trying 'CMIP'...");
      if (nc_inq_varid(ncid, "CMIP", &cmip_varid) != NC_NOERR)
      {
         logger_->error("Failed to query 'CMIP' variable ID");
         query_ok = false;
      }
   }

   if (!query_ok)
   {
      logger_->error(
         "Failed to query required GOES NetCDF variable IDs. Printing all "
         "available variable names:");
      int nvars = 0;
      if (nc_inq_nvars(ncid, &nvars) == NC_NOERR)
      {
         for (int i = 0; i < nvars; ++i)
         {
            char var_name[NC_MAX_NAME + 1];
            if (nc_inq_varname(ncid, i, var_name) == NC_NOERR)
            {
               logger_->error("  Variable [{}]: {}", i, var_name);
            }
         }
      }
      nc_close(ncid);
      return std::nullopt;
   }

   // Query projection attributes
   double lon_origin         = 0.0;
   double r_eq               = 0.0;
   double r_pol              = 0.0;
   double perspective_height = 0.0;

   if (nc_get_att_double(
          ncid, proj_varid, "longitude_of_projection_origin", &lon_origin) !=
          NC_NOERR ||
       nc_get_att_double(ncid, proj_varid, "semi_major_axis", &r_eq) !=
          NC_NOERR ||
       nc_get_att_double(ncid, proj_varid, "semi_minor_axis", &r_pol) !=
          NC_NOERR ||
       nc_get_att_double(
          ncid, proj_varid, "perspective_point_height", &perspective_height) !=
          NC_NOERR)
   {
      logger_->error(
         "Failed to read projection attributes from goes_imager_projection");
      nc_close(ncid);
      return std::nullopt;
   }

   double lambda_0 = lon_origin * M_PI / 180.0;
   double H        = perspective_height + r_eq;

   // Query dimensions
   int    x_dimids[1] = {0};
   size_t nx          = 0;
   if (nc_inq_vardimid(ncid, x_varid, x_dimids) != NC_NOERR ||
       nc_inq_dimlen(ncid, x_dimids[0], &nx) != NC_NOERR)
   {
      logger_->error("Failed to query dimension for x");
      nc_close(ncid);
      return std::nullopt;
   }

   int    y_dimids[1] = {0};
   size_t ny          = 0;
   if (nc_inq_vardimid(ncid, y_varid, y_dimids) != NC_NOERR ||
       nc_inq_dimlen(ncid, y_dimids[0], &ny) != NC_NOERR)
   {
      logger_->error("Failed to query dimension for y");
      nc_close(ncid);
      return std::nullopt;
   }

   // Get x and y scaling attributes
   double x_scale  = 1.0;
   double x_offset = 0.0;
   nc_get_att_double(ncid, x_varid, "scale_factor", &x_scale);
   nc_get_att_double(ncid, x_varid, "add_offset", &x_offset);

   double y_scale  = 1.0;
   double y_offset = 0.0;
   nc_get_att_double(ncid, y_varid, "scale_factor", &y_scale);
   nc_get_att_double(ncid, y_varid, "add_offset", &y_offset);

   // Read raw x and y arrays
   std::vector<double> x_vals(nx);
   if (nc_get_var_double(ncid, x_varid, x_vals.data()) != NC_NOERR)
   {
      logger_->error("Failed to read x values");
      nc_close(ncid);
      return std::nullopt;
   }
   for (size_t i = 0; i < nx; ++i)
   {
      x_vals[i] = x_vals[i] * x_scale + x_offset;
   }

   std::vector<double> y_vals(ny);
   if (nc_get_var_double(ncid, y_varid, y_vals.data()) != NC_NOERR)
   {
      logger_->error("Failed to read y values");
      nc_close(ncid);
      return std::nullopt;
   }
   for (size_t i = 0; i < ny; ++i)
   {
      y_vals[i] = y_vals[i] * y_scale + y_offset;
   }

   // Query CMI variable type and read data
   // Supports both short-packed (with scale_factor/add_offset) and
   // float/double-stored (already in physical units) CMI data
   nc_type cmip_type;
   nc_inq_vartype(ncid, cmip_varid, &cmip_type);

   std::vector<double> cmip_values(ny * nx);
   bool                read_ok = false;

   if (cmip_type == NC_FLOAT)
   {
      std::vector<float> cmip_raw(ny * nx);
      if (nc_get_var_float(ncid, cmip_varid, cmip_raw.data()) == NC_NOERR)
      {
         std::copy(cmip_raw.begin(), cmip_raw.end(), cmip_values.begin());
         read_ok = true;
      }
      else
      {
         logger_->error("Failed to read CMI data as NC_FLOAT");
      }
   }
   else if (cmip_type == NC_DOUBLE)
   {
      if (nc_get_var_double(ncid, cmip_varid, cmip_values.data()) == NC_NOERR)
      {
         read_ok = true;
      }
      else
      {
         logger_->error("Failed to read CMI data as NC_DOUBLE");
      }
   }
   else // NC_SHORT (and other packed integer types)
   {
      double cmip_scale  = 1.0;
      double cmip_offset = 0.0;
      nc_get_att_double(ncid, cmip_varid, "scale_factor", &cmip_scale);
      nc_get_att_double(ncid, cmip_varid, "add_offset", &cmip_offset);

      std::vector<short> cmip_raw(ny * nx);
      if (nc_get_var_short(ncid, cmip_varid, cmip_raw.data()) == NC_NOERR)
      {
         for (size_t i = 0; i < cmip_values.size(); ++i)
         {
            cmip_values[i] = cmip_raw[i] * cmip_scale + cmip_offset;
         }
         read_ok = true;
      }
      else
      {
         logger_->error("Failed to read CMI data as NC_SHORT");
      }
   }

   if (!read_ok)
   {
      logger_->error("Failed to read CMI variable data (NetCDF type={})",
                     static_cast<int>(cmip_type));
      nc_close(ncid);
      return std::nullopt;
   }

   nc_close(ncid);

   // Downsample to keep memory usage and performance optimized
   size_t step = 1;
   if (nx > 3000)
   {
      step = (nx + 2999) / 3000;
   }

   size_t cols = (nx - 1) / step;
   size_t rows = (ny - 1) / step;

   SatelliteData satelliteData;
   satelliteData.vertices.reserve(rows * cols * 12);
   satelliteData.moments.reserve(rows * cols * 6);

   auto project =
      [&](size_t x_idx, size_t y_idx, double& lat, double& lon) -> bool
   {
      double x = x_vals[x_idx];
      double y = y_vals[y_idx];

      double cos_x = std::cos(x);
      double sin_x = std::sin(x);
      double cos_y = std::cos(y);
      double sin_y = std::sin(y);

      double a =
         sin_x * sin_x +
         cos_x * cos_x *
            (cos_y * cos_y + (r_eq * r_eq) / (r_pol * r_pol) * sin_y * sin_y);
      double b     = -2.0 * H * cos_x * cos_y;
      double c_val = H * H - r_eq * r_eq;

      double discriminant = b * b - 4.0 * a * c_val;
      if (discriminant < 0.0)
      {
         return false;
      }

      double r_s = (-b - std::sqrt(discriminant)) / (2.0 * a);
      double s_x = r_s * cos_x * cos_y;
      double s_y = -r_s * sin_x;
      double s_z = r_s * cos_x * sin_y;

      double lambda = lambda_0 - std::atan2(s_y, H - s_x);
      double phi    = std::atan((r_eq * r_eq) / (r_pol * r_pol) * s_z /
                             std::sqrt((H - s_x) * (H - s_x) + s_y * s_y));

      lat = phi * 180.0 / M_PI;
      lon = lambda * 180.0 / M_PI;
      return true;
   };

   for (size_t r = 0; r < rows; ++r)
   {
      size_t y0_idx = r * step;
      size_t y1_idx = std::min((r + 1) * step, ny - 1);

      for (size_t c = 0; c < cols; ++c)
      {
         size_t x0_idx = c * step;
         size_t x1_idx = std::min((c + 1) * step, nx - 1);

         double raw_val = cmip_values[y0_idx * nx + x0_idx];

         // Skip fill/invalid values using physical-range check.
         // For short-packed data, fill values unpack to far-outside values;
         // for float-stored data, fill values are typically NaN or very
         // negative numbers like -999.0.
         if (isInfrared && (raw_val < 50.0 || raw_val > 400.0))
         {
            continue;
         }
         if (!isInfrared && (raw_val < -0.5 || raw_val > 2.0))
         {
            continue;
         }

         double lat00, lon00;
         double lat10, lon10;
         double lat01, lon01;
         double lat11, lon11;

         if (!project(x0_idx, y0_idx, lat00, lon00) ||
             !project(x1_idx, y0_idx, lat10, lon10) ||
             !project(x0_idx, y1_idx, lat01, lon01) ||
             !project(x1_idx, y1_idx, lat11, lon11))
         {
            continue;
         }

         std::uint8_t moment_val = 0;

         if (isInfrared)
         {
            double t   = std::clamp(raw_val, 180.0, 330.0);
            moment_val = static_cast<std::uint8_t>(
               std::round((330.0 - t) / 150.0 * 255.0));
         }
         else
         {
            double ref = std::clamp(raw_val, 0.0, 1.0);
            moment_val = static_cast<std::uint8_t>(std::round(ref * 255.0));
         }

         // Triangle 1
         satelliteData.vertices.push_back(static_cast<float>(lat00));
         satelliteData.vertices.push_back(static_cast<float>(lon00));
         satelliteData.vertices.push_back(static_cast<float>(lat10));
         satelliteData.vertices.push_back(static_cast<float>(lon10));
         satelliteData.vertices.push_back(static_cast<float>(lat11));
         satelliteData.vertices.push_back(static_cast<float>(lon11));

         // Triangle 2
         satelliteData.vertices.push_back(static_cast<float>(lat00));
         satelliteData.vertices.push_back(static_cast<float>(lon00));
         satelliteData.vertices.push_back(static_cast<float>(lat01));
         satelliteData.vertices.push_back(static_cast<float>(lon01));
         satelliteData.vertices.push_back(static_cast<float>(lat11));
         satelliteData.vertices.push_back(static_cast<float>(lon11));

         for (size_t m = 0; m < 6; ++m)
         {
            satelliteData.moments.push_back(moment_val);
         }
      }
   }

   logger_->debug(
      "Successfully processed satellite imagery scene: {} vertices, {} moments",
      satelliteData.vertices.size() / 2,
      satelliteData.moments.size());

   return satelliteData;
}

} // namespace util
} // namespace scwx
