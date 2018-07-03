//
//  model.h
//
//  Cave survey model.
//
//  Copyright (C) 2000-2003,2005 Mark R. Shinwell
//  Copyright (C) 2001-2003,2004,2005,2006,2010,2011,2012,2013,2014,2015,2016 Olly Betts
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

#ifndef model_h
#define model_h

#include "wx.h"

#include "labelinfo.h"
#include "vector3.h"

#include <ctime>
#include <list>
#include <set>
#include <vector>

using namespace std;

class MainFrm;

class PointInfo : public Point {
    int date;

public:
    PointInfo() : Point(), date(-1) { }
    explicit PointInfo(const img_point & pt) : Point(pt), date(-1) { }
    PointInfo(const img_point & pt, int date_) : Point(pt), date(date_) { }
    PointInfo(const Point & p, int date_) : Point(p), date(date_) { }
    int GetDate() const { return date; }
};

class XSect {
    friend class MainFrm;
    const LabelInfo* stn;
    int date;
    double l, r, u, d;
    double right_bearing;

public:
    XSect(const LabelInfo* stn_, int date_,
	  double l_, double r_, double u_, double d_)
	: stn(stn_), date(date_), l(l_), r(r_), u(u_), d(d_), right_bearing(0) { }
    double GetL() const { return l; }
    double GetR() const { return r; }
    double GetU() const { return u; }
    double GetD() const { return d; }
    double get_right_bearing() const { return right_bearing; }
    void set_right_bearing(double right_bearing_) {
	right_bearing = right_bearing_;
    }
    int GetDate() const { return date; }
    wxString GetLabel() const { return stn->GetText(); }
    const Point& GetPoint() const { return *stn; }
    double GetX() const { return stn->GetX(); }
    double GetY() const { return stn->GetY(); }
    double GetZ() const { return stn->GetZ(); }
    friend Vector3 operator-(const XSect& a, const XSect& b);
};

inline Vector3 operator-(const XSect& a, const XSect& b) {
    return *(a.stn) - *(b.stn);
}

class traverse : public vector<PointInfo> {
  public:
    int n_legs;
    // Bitmask of img_FLAG_SURFACE, img_FLAG_SPLAY and img_FLAG_DUPLICATE.
    int flags;
    double length;
    double E, H, V;
    wxString name;

    explicit
    traverse(const char* name_)
	: n_legs(0), flags(0),
	  length(0), E(-1), H(-1), V(-1),
	  name(name_, wxConvUTF8) {
	if (name.empty() && !name_[0]) {
	    // If name isn't valid UTF-8 then this conversion will
	    // give an empty string.  In this case, assume that the
	    // label is CP1252 (the Microsoft superset of ISO8859-1).
	    static wxCSConv ConvCP1252(wxFONTENCODING_CP1252);
	    name = wxString(name_, ConvCP1252);
	    if (name.empty()) {
		// Or if that doesn't work (ConvCP1252 doesn't like
		// strings with some bytes in) let's just go for
		// ISO8859-1.
		name = wxString(name_, wxConvISO8859_1);
	    }
	}
    }
};

class SurveyFilter {
    std::set<wxString, std::greater<wxString>> filters;
    wxChar separator = 0;

  public:
    SurveyFilter() {}

    void add(const wxString& survey) { filters.insert(survey); }

    void remove(const wxString& survey) { filters.erase(survey); }

    void clear() { filters.clear(); }

    bool empty() const { return filters.empty(); }

    void SetSeparator(wxChar separator_) { separator = separator_; }

    bool CheckVisible(const wxString& name) const;
};

/// Cave model.
class Model {
    list<traverse> traverses[8];
    list<vector<XSect>> tubes;

  public: // FIXME
    list<LabelInfo*> m_Labels;

  private:
    Vector3 m_Ext;
    double m_DepthMin, m_DepthExt;
    int m_DateMin, m_DateExt;
    bool complete_dateinfo;
    int m_NumEntrances = 0;
    int m_NumFixedPts = 0;
    int m_NumExportedPts = 0;
    bool m_HasUndergroundLegs = false;
    bool m_HasSplays = false;
    bool m_HasDupes = false;
    bool m_HasSurfaceLegs = false;
    bool m_HasErrorInformation = false;
    bool m_IsExtendedElevation = false;

    // Character separating survey levels (often '.')
    wxChar m_separator;

    wxString m_Title, m_cs_proj, m_DateStamp;

    time_t m_DateStamp_numeric;

    Vector3 m_Offset;

  public:
    int Load(const wxString& file, const wxString& prefix);

    void CentreDataset(const Vector3& vmin);

    const Vector3& GetExtent() const { return m_Ext; }

    const wxString& GetSurveyTitle() const { return m_Title; }

    const wxString& GetDateString() const { return m_DateStamp; }

    time_t GetDateStamp() const { return m_DateStamp_numeric; }

    double GetDepthExtent() const { return m_DepthExt; }
    double GetDepthMin() const { return m_DepthMin; }

    bool HasCompleteDateInfo() const { return complete_dateinfo; }
    int GetDateExtent() const { return m_DateExt; }
    int GetDateMin() const { return m_DateMin; }

    int GetNumFixedPts() const { return m_NumFixedPts; }
    int GetNumExportedPts() const { return m_NumExportedPts; }
    int GetNumEntrances() const { return m_NumEntrances; }

    bool HasUndergroundLegs() const { return m_HasUndergroundLegs; }
    bool HasSplays() const { return m_HasSplays; }
    bool HasDupes() const { return m_HasDupes; }
    bool HasSurfaceLegs() const { return m_HasSurfaceLegs; }
    bool HasTubes() const { return !tubes.empty(); }
    bool HasErrorInformation() const { return m_HasErrorInformation; }

    bool IsExtendedElevation() const { return m_IsExtendedElevation; }

    wxChar GetSeparator() const { return m_separator; }

    const wxString& GetCSProj() const { return m_cs_proj; }

    const Vector3& GetOffset() const { return m_Offset; }

    list<traverse>::const_iterator
    traverses_begin(unsigned flags, const SurveyFilter* filter) const {
	if (flags >= sizeof(traverses)) return traverses[0].end();
	auto it = traverses[flags].begin();
	if (filter) {
	    while (it != traverses[flags].end() &&
		   !filter->CheckVisible(it->name)) {
		++it;
	    }
	}
	return it;
    }

    list<traverse>::const_iterator
    traverses_next(unsigned flags, const SurveyFilter* filter,
		   list<traverse>::const_iterator it) const {
	++it;
	if (filter) {
	    while (it != traverses[flags].end() &&
		   !filter->CheckVisible(it->name)) {
		++it;
	    }
	}
	return it;
    }

    list<traverse>::const_iterator traverses_end(unsigned flags) const {
	if (flags >= sizeof(traverses)) flags = 0;
	return traverses[flags].end();
    }

    list<vector<XSect>>::const_iterator tubes_begin() const {
	return tubes.begin();
    }

    list<vector<XSect>>::const_iterator tubes_end() const {
	return tubes.end();
    }

    list<vector<XSect>>::iterator tubes_begin() {
	return tubes.begin();
    }

    list<vector<XSect>>::iterator tubes_end() {
	return tubes.end();
    }

    list<LabelInfo*>::const_iterator GetLabels() const {
	return m_Labels.begin();
    }

    list<LabelInfo*>::const_iterator GetLabelsEnd() const {
	return m_Labels.end();
    }

    list<LabelInfo*>::const_reverse_iterator GetRevLabels() const {
	return m_Labels.rbegin();
    }

    list<LabelInfo*>::const_reverse_iterator GetRevLabelsEnd() const {
	return m_Labels.rend();
    }

    list<LabelInfo*>::iterator GetLabelsNC() {
	return m_Labels.begin();
    }

    list<LabelInfo*>::iterator GetLabelsNCEnd() {
	return m_Labels.end();
    }
};

#endif
