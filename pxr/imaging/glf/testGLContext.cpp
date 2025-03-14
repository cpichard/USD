//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/glf/testGLContext.h"

#include "pxr/base/tf/diagnostic.h"

#ifdef PXR_X11_SUPPORT_ENABLED
#include <GL/glx.h>
#endif


PXR_NAMESPACE_OPEN_SCOPE

#ifdef PXR_X11_SUPPORT_ENABLED

class Glf_TestGLContextPrivate {
public:
    Glf_TestGLContextPrivate( Glf_TestGLContextPrivate const * other=NULL );

    void  makeCurrent( ) const;
    
    bool isValid();

    bool operator==(const Glf_TestGLContextPrivate& rhs) const
    {
        return _dpy == rhs._dpy && _context == rhs._context;
    }

    static const Glf_TestGLContextPrivate * currentContext();

    static bool areSharing( const Glf_TestGLContextPrivate * context1, 
        const Glf_TestGLContextPrivate * context2 );

private:
    Display * _dpy;
    
    GLXContext _context;
    
    Glf_TestGLContextPrivate const * _sharedContext;

    static GLXWindow _win;

    static Glf_TestGLContextPrivate const * _currenGLContext;
};

Glf_TestGLContextPrivate const * Glf_TestGLContextPrivate::_currenGLContext=NULL;
GLXWindow Glf_TestGLContextPrivate::_win=0;

Glf_TestGLContextPrivate::Glf_TestGLContextPrivate( Glf_TestGLContextPrivate const * other ) 
    : _dpy(NULL), _context(NULL)
{ 
    static int attribs[] = { GLX_DOUBLEBUFFER, GLX_RGBA_BIT, 
            GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
            GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 4, None };

    _dpy = XOpenDisplay(0);

    int n;
    GLXFBConfig * fbConfigs = glXChooseFBConfig( _dpy, 
        DefaultScreen(_dpy), attribs, &n );

    GLXContext share = other ? other->_context : 0;

    _context = glXCreateNewContext( _dpy,
        fbConfigs[0], GLX_RGBA_TYPE, share, true);
	
    _sharedContext=other ? other : this;

    if (!_win) {
        XVisualInfo * vi = glXGetVisualFromFBConfig( _dpy, fbConfigs[0] );

	XSetWindowAttributes  swa;
	swa.colormap = XCreateColormap(_dpy, RootWindow(_dpy, vi->screen),
             vi->visual, AllocNone);
	swa.border_pixel = 0;
	swa.event_mask = StructureNotifyMask;

	Window xwin = XCreateWindow( _dpy, RootWindow(_dpy, vi->screen), 
            0, 0, 256, 256, 0, vi->depth, InputOutput, vi->visual, 
	    CWBorderPixel|CWColormap|CWEventMask, &swa );

	_win = glXCreateWindow( _dpy, fbConfigs[0], xwin, NULL );
    }
}

void 
Glf_TestGLContextPrivate::makeCurrent( ) const
{
    glXMakeContextCurrent(_dpy, _win, _win, _context);

    _currenGLContext=this;
}

bool
Glf_TestGLContextPrivate::isValid()
{   
    return _context!=NULL; 
}

const Glf_TestGLContextPrivate * 
Glf_TestGLContextPrivate::currentContext()
{
    return _currenGLContext;
}

bool 
Glf_TestGLContextPrivate::areSharing( const Glf_TestGLContextPrivate * context1, const Glf_TestGLContextPrivate * context2 )
{
    if (!context1 || !context2)
        return false;

    return context1->_sharedContext==context2->_sharedContext;
}
#else // PXR_X11_SUPPORT_ENABLED

class Glf_TestGLContextPrivate
{
public:
    Glf_TestGLContextPrivate(Glf_TestGLContextPrivate const * other = nullptr);

    void makeCurrent() const;

    bool isValid();

    bool operator==(const Glf_TestGLContextPrivate& rhs) const;

    static const Glf_TestGLContextPrivate * currentContext();

    static bool areSharing(const Glf_TestGLContextPrivate * context1,
                           const Glf_TestGLContextPrivate * context2);

private:
    Glf_TestGLContextPrivate const * _sharedContext;

    static Glf_TestGLContextPrivate const * _currenGLContext;
};

Glf_TestGLContextPrivate const * Glf_TestGLContextPrivate::_currenGLContext =
    nullptr;

Glf_TestGLContextPrivate::Glf_TestGLContextPrivate(
    Glf_TestGLContextPrivate const * other)
{
    _sharedContext = other ? other : this;
}

void
Glf_TestGLContextPrivate::makeCurrent() const
{
    _currenGLContext = this;
}

bool
Glf_TestGLContextPrivate::isValid()
{
    return true;
}

bool
Glf_TestGLContextPrivate::operator==(const Glf_TestGLContextPrivate& rhs) const
{
    return true;
}

const Glf_TestGLContextPrivate *
Glf_TestGLContextPrivate::currentContext()
{
    return _currenGLContext;
}

bool
Glf_TestGLContextPrivate::areSharing(
    const Glf_TestGLContextPrivate * context1,
    const Glf_TestGLContextPrivate * context2)
{
    if (!context1 || !context2) {
        return false;
    }

    return context1->_sharedContext == context2->_sharedContext;
}

#endif // PXR_X11_SUPPORT_ENABLED


Glf_TestGLContextPrivate *
_GetSharedContext()
{
    static Glf_TestGLContextPrivate* sharedCtx = new Glf_TestGLContextPrivate();
    return sharedCtx;
}

//
// GlfTestGLContextRegistrationInterface
//

class GlfTestGLContextRegistrationInterface :
    public GlfGLContextRegistrationInterface {
public:
    GlfTestGLContextRegistrationInterface();
    virtual ~GlfTestGLContextRegistrationInterface();

    // GlfGLContextRegistrationInterface overrides
    virtual GlfGLContextSharedPtr GetShared();
    virtual GlfGLContextSharedPtr GetCurrent();
};

GlfTestGLContextRegistrationInterface::GlfTestGLContextRegistrationInterface()
{
    // Do nothing
}

GlfTestGLContextRegistrationInterface::~GlfTestGLContextRegistrationInterface()
{
    // Do nothing
}

GlfGLContextSharedPtr
GlfTestGLContextRegistrationInterface::GetShared()
{
    return GlfGLContextSharedPtr(new GlfTestGLContext(_GetSharedContext()));
}

GlfGLContextSharedPtr
GlfTestGLContextRegistrationInterface::GetCurrent()
{
    if (const Glf_TestGLContextPrivate* context =
            Glf_TestGLContextPrivate::currentContext()) {
        return GlfGLContextSharedPtr(new GlfTestGLContext(context));
    }
    return GlfGLContextSharedPtr();
}

//
// GlfTestGLContext
//

GlfTestGLContextSharedPtr 
GlfTestGLContext::Create( GlfTestGLContextSharedPtr const & share )
{
    Glf_TestGLContextPrivate * ctx = new Glf_TestGLContextPrivate( 
        share && share->_context ? share->_context : NULL );
    return GlfTestGLContextSharedPtr( new GlfTestGLContext( ctx ) );
}

void
GlfTestGLContext::RegisterGLContextCallbacks()
{
    new GlfTestGLContextRegistrationInterface;
}

GlfTestGLContext::GlfTestGLContext(Glf_TestGLContextPrivate const * context) :
    _context(const_cast<Glf_TestGLContextPrivate *>(context))
{
}

bool
GlfTestGLContext::IsValid() const
{
    return (_context && _context->isValid());
}

void
GlfTestGLContext::_MakeCurrent()
{
    _context->makeCurrent();
}

bool
GlfTestGLContext::_IsSharing(GlfGLContextSharedPtr const & otherContext)const
{
    GlfTestGLContextSharedPtr otherGlfTestGLContext =
        std::dynamic_pointer_cast<GlfTestGLContext>(otherContext);
    return (otherGlfTestGLContext &&
            Glf_TestGLContextPrivate::areSharing(_context, otherGlfTestGLContext->_context));
}

bool
GlfTestGLContext::_IsEqual(GlfGLContextSharedPtr const &rhs) const
{
    if (const GlfTestGLContext* rhsRaw =
            dynamic_cast<const GlfTestGLContext*>(rhs.get())) {
        return *_context == *rhsRaw->_context;
    }
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
