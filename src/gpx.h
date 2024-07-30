/* gpx.h
 * Export from Aven as GPX.
 */

/* Copyright (C) 2005,2013,2014,2015,2016 Olly Betts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "exportfilter.h"

#include <proj.h>

class GPX : public ExportFilter {
    PJ* pj = NULL;
    bool in_trkseg = false;
    const char * trk_name = NULL;
  public:
    explicit GPX(const char * input_datum);
    ~GPX();
    const int * passes() const override;
    void header(const char *, const char *, time_t,
		double, double, double,
		double, double, double) override;
    void line(const img_point *, const img_point *, unsigned, bool) override;
    void label(const img_point *, const wxString&, bool, int) override;
    void footer() override;
};
