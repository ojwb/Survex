//
//  gla.h
//
//  Header file for the GLA abstraction layer.
//
//  Copyright (C) 2002 Mark R. Shinwell.
//  Copyright (C) 2003,2004,2005,2006,2007,2011,2012,2014,2017,2018 Olly Betts
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

#ifndef gla_h
#define gla_h

#include <string>
#include <vector>

using namespace std;

#include "wx.h"
#include "aventypes.h"
#include "vector3.h"

#include "glbitmapfont.h"

class GfxCore;

string GetGLSystemDescription();

// #define GLA_DEBUG

typedef Double glaCoord;

typedef GLfloat glaTexCoord;

// Colours for drawing.  Don't reorder these!
enum gla_colour {
    col_BLACK = 0,
    col_GREY,
    col_LIGHT_GREY,
    col_LIGHT_GREY_2,
    col_DARK_GREY,
    col_WHITE,
    col_TURQUOISE,
    col_GREEN,
    col_INDICATOR_1,
    col_INDICATOR_2,
    col_YELLOW,
    col_RED,
    col_BLUE,
    col_LAST // must be the last entry here
};

class GLAPen {
    friend class GLACanvas; // allow direct access to components

    double components[3]; // red, green, blue

public:
    GLAPen();

    void SetColour(double red, double green, double blue); // arguments in range 0 to 1.0
    void Interpolate(const GLAPen&, double how_far);

    double GetRed() const;
    double GetGreen() const;
    double GetBlue() const;
};

class GLAList {
    GLuint gl_list;
    unsigned int flags;
  public:
    GLAList() : gl_list(0), flags(0) { }
    GLAList(GLuint gl_list_, unsigned int flags_)
	: gl_list(gl_list_), flags(flags_) { }
    operator bool() { return gl_list != 0; }
    bool need_to_generate();
    void finalise(unsigned int list_flags);
    bool DrawList() const;
    void invalidate_if(unsigned int mask) {
	// If flags == NEVER_CACHE, the list won't be invalidated (unless
	// mask is 0, which isn't a normal thing to pass).
	if (flags & mask)
	    flags = 0;
    }
};

class GLACanvas : public wxGLCanvas {
    friend class GLAList; // For flag values.

    wxGLContext ctx;

#ifdef GLA_DEBUG
    int m_Vertices;
#endif

    GLdouble modelview_matrix[16];
    GLdouble projection_matrix[16];
    GLint viewport[4];

    // Viewing volume diameter:
    glaCoord m_VolumeDiameter;

    // Parameters for plotting data:
    Double m_Pan, m_Tilt;
    Double m_Scale;
    Vector3 m_Translation;

    BitmapFont m_Font;

    GLUquadric* m_Quadric;

    GLuint m_Texture;
    GLuint m_BlobTexture;
    GLuint m_CrossTexture;

    Double alpha;

    bool m_SmoothShading;
    bool m_Textured;
    bool m_Perspective;
    bool m_Fog;
    bool m_AntiAlias;
    bool save_hints;
    enum { UNKNOWN = 0, POINT = 'P', LINES = 'L', SPRITE = 'S' };
    int blob_method;
    int cross_method;

    int x_size;
    int y_size;

    vector<GLAList> drawing_lists;

    enum {
	INVALIDATE_ON_SCALE = 1,
	INVALIDATE_ON_X_RESIZE = 2,
	INVALIDATE_ON_Y_RESIZE = 4,
	NEVER_CACHE = 8,
	CACHED = 16
    };
    mutable unsigned int list_flags;

    wxString vendor, renderer;

    bool CheckVisualFidelity(const unsigned char * target) const;

public:
    GLACanvas(wxWindow* parent, int id);
    ~GLACanvas();

    static bool check_visual();

    void FirstShow();

    void Clear();
    void StartDrawing();
    void FinishDrawing();

    void SetVolumeDiameter(glaCoord diameter);
    void SetDataTransform();
    void SetIndicatorTransform();

    void DrawList(unsigned int l);
    void DrawListZPrepass(unsigned int l);
    void DrawList2D(unsigned int l, glaCoord x, glaCoord y, Double rotation);
    void InvalidateList(unsigned int l) {
	if (l < drawing_lists.size()) {
	    // Invalidate any existing cached list.
	    drawing_lists[l].invalidate_if(CACHED);
	}
    }

    virtual void GenerateList(unsigned int l) = 0;

    void SetColour(const GLAPen& pen, double rgb_scale);
    void SetColour(const GLAPen& pen);
    void SetColour(gla_colour colour, double rgb_scale);
    void SetColour(gla_colour colour);
    void SetAlpha(double new_alpha) { alpha = new_alpha; }

    void DrawText(glaCoord x, glaCoord y, glaCoord z, const wxString& str);
    void DrawIndicatorText(int x, int y, const wxString& str);
    void GetTextExtent(const wxString& str, int * x_ext, int * y_ext) const;

    void BeginQuadrilaterals();
    void EndQuadrilaterals();
    void BeginLines();
    void EndLines();
    void BeginTriangleStrip();
    void EndTriangleStrip();
    void BeginTriangles();
    void EndTriangles();
    void BeginPolyline();
    void EndPolyline();
    void BeginPolyloop();
    void EndPolyloop();
    void BeginPolygon();
    void EndPolygon();
    void BeginBlobs();
    void EndBlobs();
    void BeginCrosses();
    void EndCrosses();

    void DrawRectangle(gla_colour fill, gla_colour edge,
		       glaCoord x0, glaCoord y0, glaCoord w, glaCoord h);
    void DrawShadedRectangle(const GLAPen & fill_bot, const GLAPen & fill_top,
			     glaCoord x0, glaCoord y0, glaCoord w, glaCoord h);
    void DrawCircle(gla_colour edge, gla_colour fill, glaCoord cx, glaCoord cy, glaCoord radius);
    void DrawSemicircle(gla_colour edge, gla_colour fill, glaCoord cx, glaCoord cy, glaCoord radius, glaCoord start);
    void DrawTriangle(gla_colour edge, gla_colour fill,
		      const Vector3 &p0, const Vector3 &p1, const Vector3 &p2);

    void DrawBlob(glaCoord x, glaCoord y, glaCoord z);
    void DrawBlob(glaCoord x, glaCoord y);
    void DrawCross(glaCoord x, glaCoord y, glaCoord z);
    void DrawRing(glaCoord x, glaCoord y);

    void PlaceVertex(const Vector3 & v, glaTexCoord tex_x, glaTexCoord tex_y) {
	PlaceVertex(v.GetX(), v.GetY(), v.GetZ(), tex_x, tex_y);
    }
    void PlaceVertex(const Vector3 & v) {
	PlaceVertex(v.GetX(), v.GetY(), v.GetZ());
    }
    void PlaceVertex(glaCoord x, glaCoord y, glaCoord z);
    void PlaceVertex(glaCoord x, glaCoord y, glaCoord z,
		     glaTexCoord tex_x, glaTexCoord tex_y);
    void PlaceIndicatorVertex(glaCoord x, glaCoord y);

    void PlaceNormal(const Vector3 &v);

    void EnableDashedLines();
    void DisableDashedLines();

    void EnableSmoothPolygons(bool filled);
    void DisableSmoothPolygons();

    void SetRotation(double pan, double tilt) {
	m_Pan = pan;
	m_Tilt = tilt;
    }
    void SetScale(Double);
    void SetTranslation(const Vector3 &v) {
	m_Translation = v;
    }
    void AddTranslation(const Vector3 &v) {
	m_Translation += v;
    }
    const Vector3 & GetTranslation() const {
	return m_Translation;
    }
    void AddTranslationScreenCoordinates(int dx, int dy);

    bool Transform(const Vector3 & v, double* x_out, double* y_out, double* z_out) const;
    void ReverseTransform(Double x, Double y, double* x_out, double* y_out, double* z_out) const;

    int GetFontSize() const { return m_Font.get_font_size(); }

    void ToggleSmoothShading();
    bool GetSmoothShading() const { return m_SmoothShading; }

    Double SurveyUnitsAcrossViewport() const;

    void ToggleTextured();
    bool GetTextured() const { return m_Textured; }

    void TogglePerspective() { m_Perspective = !m_Perspective; }
    bool GetPerspective() const { return m_Perspective; }

    void ToggleFog() { m_Fog = !m_Fog; }
    bool GetFog() const { return m_Fog; }

    void ToggleAntiAlias() { m_AntiAlias = !m_AntiAlias; }
    bool GetAntiAlias() const { return m_AntiAlias; }

    bool SaveScreenshot(const wxString & fnm, wxBitmapType type) const;

    void ReadPixels(int width, int height, unsigned char * buf) const;

    void PolygonOffset(bool on) const;

    int GetXSize() const { list_flags |= INVALIDATE_ON_X_RESIZE; return x_size; }
    int GetYSize() const { list_flags |= INVALIDATE_ON_Y_RESIZE; return y_size; }

    void OnSize(wxSizeEvent & event);

    glaCoord GetVolumeDiameter() const { return m_VolumeDiameter; }

private:
    DECLARE_EVENT_TABLE()
};

#endif
