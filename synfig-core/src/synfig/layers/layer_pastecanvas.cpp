/* === S Y N F I G ========================================================= */
/*!	\file layer_pastecanvas.cpp
**	\brief Implementation of the "Paste Canvas" layer
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**	Copyright (c) 2011-2013 Carlos López
**	......... ... 2014-2017 Ivan Mahonin
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "layer_pastecanvas.h"

#include <synfig/general.h>
#include <synfig/localization.h>

#include <synfig/cairo_renddesc.h>
#include <synfig/canvas.h>
#include <synfig/context.h>
#include <synfig/paramdesc.h>
#include <synfig/renddesc.h>
#include <synfig/time.h>
#include <synfig/string.h>
#include <synfig/surface.h>
#include <synfig/value.h>
#include <synfig/valuenode.h>

#include <synfig/rendering/common/task/taskblend.h>
#include <synfig/rendering/common/task/tasktransformation.h>
#include <synfig/rendering/common/task/taskpixelprocessor.h>
#include <synfig/rendering/primitive/transformationaffine.h>

#endif

/* === U S I N G =========================================================== */

using namespace etl;
using namespace std;
using namespace synfig;

/* === M A C R O S ========================================================= */

#define MAX_DEPTH 10

// if this isn't defined, the 'dead heads' in examples/pirates.sifz don't render properly
#define SYNFIG_CLIP_PASTECANVAS

//#ifdef __APPLE__
//#undef SYNFIG_CLIP_PASTECANVAS
//#endif

/* === C L A S S E S ======================================================= */

class depth_counter	// Makes our recursive depth counter exception-safe
{
	int *depth;
public:
	depth_counter(int &x):depth(&x) { (*depth)++; }
	~depth_counter() { (*depth)--; }
};

/* === G L O B A L S ======================================================= */

/* === M E T H O D S ======================================================= */

Layer_PasteCanvas::Layer_PasteCanvas(Real amount, Color::BlendMethod blend_method):
	Layer_Composite(amount, blend_method),
	param_origin(Point()),
	param_transformation(Transformation()),
	param_time_dilation(Real(1)),
	param_time_offset(Time(0)),
	param_outline_grow(Real(0)),
	param_children_lock(bool(false)),
	depth(0),
	extra_reference(false)
{
	SET_INTERPOLATION_DEFAULTS();
	SET_STATIC_DEFAULTS();
}

Layer_PasteCanvas::~Layer_PasteCanvas()
{
/*	if(sub_canvas)
		sub_canvas->parent_set.erase(this);
*/

	//if(sub_canvas)DEBUGINFO(strprintf("%d",sub_canvas->count()));

	set_sub_canvas(0);

	//if(sub_canvas && (sub_canvas->is_inline() || !get_canvas() || get_canvas()->get_root()!=sub_canvas->get_root()))
	//if(extra_reference)
	//	sub_canvas->unref();
}

String
Layer_PasteCanvas::get_local_name()const
{
	if(!sub_canvas || sub_canvas->is_inline()) return String();
	if(sub_canvas->get_root()==get_canvas()->get_root()) return sub_canvas->get_id();
	return sub_canvas->get_file_name();
}

Layer::Vocab
Layer_PasteCanvas::get_param_vocab()const
{
	Layer::Vocab ret(Layer_Composite::get_param_vocab());

	ret.push_back(ParamDesc("origin")
		.set_local_name(_("Origin"))
		.set_description(_("Position offset"))
	);
	
	ret.push_back(ParamDesc("transformation")
		.set_local_name(_("Transformation"))
		.set_description(_("Position, rotation, skew and scale"))
	);

	ret.push_back(ParamDesc("canvas")
		.set_local_name(_("Canvas"))
		.set_description(_("Group content"))
	);

	ret.push_back(ParamDesc("time_dilation")
		.set_local_name(_("Speed"))
		.set_description(_("Multiplier to speed up, slow down, freeze, or reverse time"))
	);

	ret.push_back(ParamDesc("time_offset")
		.set_local_name(_("Time Offset"))
		.set_description(_("Time Offset to apply to the context"))
	);

	ret.push_back(ParamDesc("children_lock")
		.set_local_name(_("Lock Selection"))
		.set_description(_("When checked prevents to select the children using the mouse click"))
		.set_static(true)
	);

	ret.push_back(ParamDesc("outline_grow")
		.set_local_name(_("Outline Grow"))
		.set_description(_("Exponential value to grow children Outline layers width"))
	);
	if(sub_canvas && !(sub_canvas->is_inline()))
	{
		ret.back().hidden();
	}

	return ret;
}

bool
Layer_PasteCanvas::set_param(const String & param, const ValueBase &value)
{
	IMPORT_VALUE(param_origin);
	IMPORT_VALUE(param_transformation);

	// IMPORT(canvas);
	if(param=="canvas" && value.can_get(Canvas::Handle()))
	{
		set_sub_canvas(value.get(Canvas::Handle()));
		return true;
	}
	//! \todo this introduces bug 1844764 if enabled; it was introduced in r954.
	// http://synfig.org/images/3/3d/Moving-waypoints.sifz is an
	// example of an animation that has its waypoints displayed
	// incorrectly without this fix; select the outer layer and drag
	// the time slider.  The linear waypoints don't take effect until
	// 5s, but the time slider appears to pass the first one at 3s.
#if 0
	if (param=="time_offset" && value.same_type_as(time_offset))
	{
		if (time_offset != value.get(Time()))
		{
			value.put(&time_offset);
			// notify that the time_offset has changed so we can update the
			// waypoint positions in parent layers
			changed();
		}
		return true;
	}
#else
	IMPORT_VALUE(param_time_dilation);
	IMPORT_VALUE(param_time_offset);
#endif

	IMPORT_VALUE(param_children_lock);
	IMPORT_VALUE_PLUS(param_outline_grow,
		if (sub_canvas)
		{
			Real sub_outline_grow = param_outline_grow.get(Real());
			sub_canvas->set_outline_grow(get_outline_grow_mark() + sub_outline_grow);
		}
	);
	return Layer_Composite::set_param(param,value);
}

void
Layer_PasteCanvas::childs_changed()
{
	if (get_canvas()) get_canvas()->signal_changed()();
	on_childs_changed();
}

void
Layer_PasteCanvas::set_sub_canvas(etl::handle<synfig::Canvas> x)
{
	if (sub_canvas)
		remove_child(sub_canvas.get());

	// if(sub_canvas && (sub_canvas->is_inline() || !get_canvas() || get_canvas()->get_root()!=sub_canvas->get_root()))
	if (extra_reference)
		sub_canvas->unref();

	childs_changed_connection.disconnect();

	if (sub_canvas != x) signal_subcanvas_changed()();

	sub_canvas=x;

	if (sub_canvas)
		childs_changed_connection=sub_canvas->signal_changed().connect(
			sigc::mem_fun(*this, &Layer_PasteCanvas::childs_changed) );

	if (sub_canvas)
		add_child(sub_canvas.get());

	if (sub_canvas && (sub_canvas->is_inline() || !get_canvas() || get_canvas()->get_root()!=sub_canvas->get_root()))
	{
		sub_canvas->ref();
		extra_reference = true;
	}
	else
		extra_reference = false;

	if (sub_canvas)
		on_canvas_set();
}

// when a pastecanvas that contains another pastecanvas is copy/pasted
// from one document to another, only the outermost pastecanvas was
// getting its renddesc set to match that of its new parent.  this
// function is used to recurse through the pastecanvas copying its
// renddesc to any pastecanvases it contains (bug #2116947, svn r2200)
void
Layer_PasteCanvas::update_renddesc()
{
	if(!get_canvas() || !sub_canvas || !sub_canvas->is_inline()) return;

	sub_canvas->rend_desc()=get_canvas()->rend_desc();
	for (IndependentContext iter = sub_canvas->get_independent_context(); !iter->empty(); iter++)
	{
		etl::handle<Layer_PasteCanvas> paste = etl::handle<Layer_PasteCanvas>::cast_dynamic(*iter);
		if (paste) paste->update_renddesc();
	}
}

// This is called whenever the parent canvas gets set/changed
void
Layer_PasteCanvas::on_canvas_set()
{
	if(get_canvas() && sub_canvas && sub_canvas->is_inline() && sub_canvas->parent()!=get_canvas())
	{
		sub_canvas->set_inline(get_canvas());
	}
}

ValueBase
Layer_PasteCanvas::get_param(const String& param)const
{
	EXPORT_VALUE(param_origin);
	EXPORT_VALUE(param_transformation);
	if (param=="canvas")
	{
		synfig::ValueBase ret(sub_canvas);
		return ret;
	}
	EXPORT_VALUE(param_time_dilation);
	EXPORT_VALUE(param_time_offset);
	EXPORT_VALUE(param_children_lock);
	EXPORT_VALUE(param_outline_grow);

	return Layer_Composite::get_param(param);
}

void
Layer_PasteCanvas::set_time_vfunc(IndependentContext context, Time time)const
{
	context.set_time(time);

	if (!sub_canvas)
		return;
	if (depth == MAX_DEPTH)
		return;
	depth_counter counter(depth);

	Real time_dilation = param_time_dilation.get(Real());
	Time time_offset = param_time_offset.get(Time());
	sub_canvas->set_time(time*time_dilation + time_offset);
}

void
Layer_PasteCanvas::load_resources_vfunc(IndependentContext context, Time time)const
{
	context.load_resources(time);

	if (!sub_canvas)
		return;
	if (depth == MAX_DEPTH)
		return;
	depth_counter counter(depth);

	Real time_dilation = param_time_dilation.get(Real());
	Time time_offset = param_time_offset.get(Time());
	sub_canvas->load_resources(time*time_dilation + time_offset);
}

void
Layer_PasteCanvas::set_outline_grow_vfunc(IndependentContext context, Real outline_grow)const
{
	context.set_outline_grow(outline_grow);

	if (!sub_canvas)
		return;
	if (depth == MAX_DEPTH)
		return;
	depth_counter counter(depth);

	Real sub_outline_grow = param_outline_grow.get(Real());
	sub_canvas->set_outline_grow(outline_grow + sub_outline_grow);
}

void
Layer_PasteCanvas::apply_z_range_to_params(ContextParams &cp)const
{
	ContextParams p;
	cp.z_range = p.z_range;
	cp.z_range_position = p.z_range_position;
	cp.z_range_depth = p.z_range_depth;
	cp.z_range_blur = p.z_range_blur;
}

synfig::Layer::Handle
Layer_PasteCanvas::hit_check(synfig::Context context, const synfig::Point &pos)const
{
	if(!sub_canvas || !get_amount())
		return context.hit_check(pos);
	if (depth == MAX_DEPTH)
		return 0;
	depth_counter counter(depth);

	Transformation transformation(get_summary_transformation());
	Point target_pos = transformation.back_transform(pos);

	CanvasBase queue;
	Context subcontext = build_context_queue(context, queue);
	if (subcontext.get_color(target_pos).get_a() >= 0.25)
		return param_children_lock.get(bool(true))
			 ? const_cast<Layer_PasteCanvas*>(this)
			 : subcontext.hit_check(target_pos);
	return context.hit_check(pos);
}

Color
Layer_PasteCanvas::get_color(Context context, const Point &pos)const
{
	if(!sub_canvas || !get_amount())
		return context.get_color(pos);
	if (depth == MAX_DEPTH)
		return Color::alpha();
	depth_counter counter(depth);

	Transformation transformation(get_summary_transformation());
	Point target_pos = transformation.back_transform(pos);

	CanvasBase queue;
	Context subcontext = build_context_queue(context, queue);
	return Color::blend( subcontext.get_color(target_pos),
			             context.get_color(pos),
						 get_amount(),
						 get_blend_method() );
}

Rect
Layer_PasteCanvas::get_bounding_rect_context_dependent(const ContextParams &context_params)const
{
	if (!sub_canvas)
		return Rect::zero();

	ContextParams subparams(context_params);
	apply_z_range_to_params(subparams);

	CanvasBase queue;
	Context subcontext = sub_canvas->get_context_sorted(subparams, queue);

	return get_summary_transformation().transform_bounds(
			subcontext.get_full_bounding_rect() );
}

Rect
Layer_PasteCanvas::get_full_bounding_rect(Context context)const
{
	if (is_disabled() || Color::is_onto(get_blend_method()) || !sub_canvas)
		return context.get_full_bounding_rect();

	CanvasBase queue;
	Rect rect = get_summary_transformation().transform_bounds(
		build_context_queue(context, queue).get_full_bounding_rect() );

	return rect | context.get_full_bounding_rect();
}

bool
Layer_PasteCanvas::accelerated_render(Context context,Surface *surface,int quality, const RendDesc &renddesc, ProgressCallback *cb)const
{
	Transformation transformation(
		get_summary_transformation().get_matrix()
	  * renddesc.get_transformation_matrix() );

	if(cb && !cb->amount_complete(0,10000)) return false;

	if(depth==MAX_DEPTH)
		// if we are at the extent of our depth,
		// then we should just return whatever is under us.
		return context.accelerated_render(surface,quality,renddesc,cb);

	depth_counter counter(depth);

	if(!sub_canvas || !get_amount())
		return context.accelerated_render(surface,quality,renddesc,cb);

	SuperCallback stageone(cb,0,4500,10000);
	SuperCallback stagetwo(cb,4500,9000,10000);
	SuperCallback stagethree(cb,9000,9999,10000);

	CanvasBase queue;
	Context canvasContext = build_context_queue(context, queue);

	if (is_solid_color())
	{
		RendDesc intermediate_desc(renddesc);
		intermediate_desc.clear_flags();
		intermediate_desc.set_transformation_matrix(transformation.get_matrix());
		return canvasContext.accelerated_render(surface,quality,intermediate_desc,&stagetwo);
	}
	else
	if (!context.accelerated_render(surface,quality,renddesc,&stageone))
		return false;

	Color::BlendMethod blend_method(get_blend_method());
	const Rect full_bounding_rect(canvasContext.get_full_bounding_rect());

	Rect inner_bounds(
	    full_bounding_rect.get_min(),
	    full_bounding_rect.get_max()
	);
	inner_bounds &= transformation.back_transform_bounds(renddesc.get_rect());
	Rect outer_bounds(transformation.transform_bounds(inner_bounds));
	outer_bounds &= renddesc.get_rect();
	if (!outer_bounds.is_valid())
		return true;
	
	Rect next_bounds( Transformation::transform_bounds(renddesc.get_transformation_matrix(), context.get_full_bounding_rect()) );

	// sometimes the user changes the parameters while we're
	// rendering, causing our pasted canvas' bounding box to shrink
	// and no longer overlap with our tile.  if that has happened,
	// let's just stop now - we'll be refreshing soon anyway
	//! \todo shouldn't a mutex ensure this isn't needed?
	// http://synfig.org/images/d/d2/Bbox-change.sifz is an example
	// that shows this happening - open the encapsulation, select the
	// 'shade', and toggle the 'invert' parameter quickly.
	// Occasionally you'll see:
	//   error: Context::accelerated_render(): Layer "shade" threw a bad_alloc exception!
	// where the shade layer tries to allocate itself a canvas of
	// negative proportions, due to changing bounding boxes.
	if (!inner_bounds.is_valid())
	{
		warning("%s:%d bounding box shrank while rendering?", __FILE__, __LINE__);
		return true;
	}

	bool blend_using_straight = false; // use 'straight' just for the central blit

	if (!etl::intersect(next_bounds,outer_bounds))
	{
		// if there's no intersection between the context and our
		// surface, and we're rendering 'onto', then we're done
		if (Color::is_onto(blend_method) && !Color::is_straight(blend_method))
			return true;

		/* 'straight' is faster than 'composite' and has the same
		 * effect if the affected area of the lower layer is
		 * transparent;  however, if we're not clipping the blit to
		 * just the bounding rectangle, the affected area is the whole
		 * tile, so we can't use this optimisation.  if we are
		 * clipping, then we can use 'straight' to blit the clipped
		 * rectangle, but we shouldn't set blend_method to 'straight',
		 * or the surrounding areas will be blanked, which we don't
		 * want.
		 */
#ifdef SYNFIG_CLIP_PASTECANVAS
		if (blend_method==Color::BLEND_COMPOSITE) blend_using_straight = true;
#endif	// SYNFIG_CLIP_PASTECANVAS
	}

	int w = renddesc.get_w();
	int h = renddesc.get_h();
	Vector tl = renddesc.get_tl();
	Vector br = renddesc.get_br();
	Vector size = br - tl;
	Real rx0 = (outer_bounds.minx - tl[0])/size[0]*w;
	Real rx1 = (outer_bounds.maxx - tl[0])/size[0]*w;
	Real ry0 = (outer_bounds.miny - tl[1])/size[1]*h;
	Real ry1 = (outer_bounds.maxy - tl[1])/size[1]*h;
	if (rx1 < rx0) { Real rx = rx0; rx0 = rx1; rx1 = rx; }
	if (ry1 < ry0) { Real ry = ry0; ry0 = ry1; ry1 = ry; }
	int x0((floor(rx0)));
	int x1((ceil(rx1)));
	int y0((floor(ry0)));
	int y1((ceil(ry1)));

	if (x0 < 0) x0 = 0; else if (x0 > w) x0 = w;
	if (x1 < 0) x1 = 0; else if (x1 > w) x1 = w;
	if (y0 < 0) y0 = 0; else if (y0 > h) y0 = h;
	if (y1 < 0) y1 = 0; else if (y1 > h) y1 = h;
	int intermediate_w = x1 - x0;
	int intermediate_h = y1 - y0;
	Vector pixel_aligned_tl(
		(Real)x0/(Real)w*size[0] + tl[0],
		(Real)y0/(Real)h*size[1] + tl[1] );
	Vector pixel_aligned_br(
		(Real)x1/(Real)w*size[0] + tl[0],
		(Real)y1/(Real)h*size[1] + tl[1] );

	if (intermediate_w > 0 && intermediate_h > 0) {
		RendDesc intermediate_desc(renddesc);
		intermediate_desc.clear_flags();
		intermediate_desc.set_transformation_matrix(transformation.get_matrix());
		intermediate_desc.set_wh(intermediate_w, intermediate_h);
		intermediate_desc.set_tl(pixel_aligned_tl);
		intermediate_desc.set_br(pixel_aligned_br);
		Surface intermediate_surface;

		//{ // TODO: remove
		//	std::ofstream of("/tmp/contours.txt", std::ios_base::app);
		//	of << "g " << x0 << " " << y0 << endl;
		//}

		if(!canvasContext.accelerated_render(&intermediate_surface,quality,intermediate_desc,&stagetwo))
			return false;

		//{ // TODO: remove
		//	std::ofstream of("/tmp/contours.txt", std::ios_base::app);
		//	of << "end" << endl;
		//}

		Surface::alpha_pen apen(surface->get_pen(x0, y0));
		apen.set_alpha(get_amount());
		apen.set_blend_method(blend_using_straight ? Color::BLEND_STRAIGHT : blend_method);
		intermediate_surface.blit_to(apen);
	}

	if(cb && !cb->amount_complete(10000,10000)) return false;
	return true;
}

///////

bool
Layer_PasteCanvas::accelerated_cairorender(Context context,cairo_t *cr, int quality, const RendDesc &renddesc, ProgressCallback *cb)const
{
	Transformation transformation(get_summary_transformation());

	if(cb && !cb->amount_complete(0,10000)) return false;

	if(depth==MAX_DEPTH)
		// if we are at the extent of our depth,
		// then we should just return whatever is under us.
		return context.accelerated_cairorender(cr,quality,renddesc,cb);

	depth_counter counter(depth);

	if(!sub_canvas || !get_amount())
		return context.accelerated_cairorender(cr,quality,renddesc,cb);

	SuperCallback stageone(cb,0,4500,10000);
	SuperCallback stagetwo(cb,4500,9000,10000);
	SuperCallback stagethree(cb,9000,9999,10000);


	bool ret;
	RendDesc workdesc(renddesc);

	// Render the background
	ret=context.accelerated_cairorender(cr, quality, renddesc, &stagethree);
	if(!ret)
		return false;

	// render the canvas to be pasted onto pastesurface
	cairo_surface_t* pastesurface=cairo_surface_create_similar_image(cairo_get_target(cr), CAIRO_FORMAT_ARGB32, workdesc.get_w(), workdesc.get_h());
	cairo_t* subcr=cairo_create(pastesurface);
	// apply the transformations form the current context
	cairo_matrix_t matrix;
	cairo_get_matrix(cr, &matrix);

	// apply the transformations form the (paste canvas) group layer
	cairo_set_matrix(subcr, &matrix);

	cairo_matrix_t cairo_transformation_matrix;
	Matrix transformation_matrix(transformation.get_matrix());
	cairo_matrix_init(
		&cairo_transformation_matrix,
		transformation_matrix.m00,
		transformation_matrix.m01,
		transformation_matrix.m10,
		transformation_matrix.m11,
		transformation_matrix.m20,
		transformation_matrix.m21 );

	cairo_transform(subcr, &cairo_transformation_matrix);

	// Effectively render the canvas content
	CanvasBase queue;
	ret=build_context_queue(context, queue).accelerated_cairorender(subcr, quality, workdesc, &stagetwo);
	// we are done apply the result to the source
	cairo_destroy(subcr);

	if(!ret)
		return false;
	// Let's paint the result with its alpha
	cairo_save(cr);

	cairo_status_t status;
	status=cairo_matrix_invert(&matrix);
	if(status) // doh! the matrix can't be inverted!
	{
		synfig::error("Can't invert current Cairo matrix!");
		return false;
	}
	// apply the inverse of the transformation of the current context to
	// compensate the pending transformations form cr to be applied.
	cairo_transform(cr, &matrix);
	cairo_set_source_surface(cr, pastesurface, 0, 0);
	cairo_paint_with_alpha_operator(cr, get_amount(), get_blend_method());

	cairo_restore(cr);
	cairo_surface_destroy(pastesurface);

	if(cb && !cb->amount_complete(10000,10000)) return false;

	return true;
}
///////


void Layer_PasteCanvas::get_times_vfunc(Node::time_set &set) const
{
	Real time_dilation=param_time_dilation.get(Real());
	Time time_offset=param_time_offset.get(Time());

	Node::time_set tset;
	if(sub_canvas) tset = sub_canvas->get_times();

	Node::time_set::iterator i = tset.begin(), end = tset.end();

	//Make sure we offset the time...
	//! \todo: SOMETHING STILL HAS TO BE DONE WITH THE OTHER DIRECTION
	//		   (recursing down the tree needs to take this into account too...)
	for(; i != end; ++i) {
#ifdef ADJUST_WAYPOINTS_FOR_TIME_OFFSET // see node.h
		if (time_dilation!=0)
		{
			TimePoint tp = *i;
			tp.set_time((tp.get_time() - time_offset) / time_dilation);
			set.insert(tp);
		}
#else
		set.insert(*i);
#endif
	}

	Layer::get_times_vfunc(set);
}

void
Layer_PasteCanvas::fill_sound_processor(SoundProcessor &soundProcessor) const
{
	if (active() && sub_canvas) sub_canvas->fill_sound_processor(soundProcessor);
}

rendering::Task::Handle
Layer_PasteCanvas::build_rendering_task_vfunc(Context context)const
{
	rendering::Task::Handle sub_task;
	if (sub_canvas)
	{
		CanvasBase sub_queue;
		Context sub_context = build_context_queue(context, sub_queue);

		rendering::TaskTransformationAffine::Handle task_transformation(new rendering::TaskTransformationAffine());
		task_transformation->transformation->matrix = get_summary_transformation().get_matrix();
		task_transformation->sub_task() = sub_context.build_rendering_task();
		sub_task = task_transformation;
		
		if (sub_canvas->get_root() != get_canvas()->get_root()) {
			rendering::TaskPixelGamma::Handle task_gamma(new rendering::TaskPixelGamma());
			task_gamma->gamma = get_canvas()->get_root()->rend_desc().get_gamma()
							  / sub_canvas->get_root()->rend_desc().get_gamma();
			task_gamma->sub_task() = sub_task;
			if (!task_gamma->is_transparent())
				sub_task = task_gamma;
		}
	}

	rendering::TaskBlend::Handle task_blend(new rendering::TaskBlend());
	task_blend->amount = get_amount() * Context::z_depth_visibility(context.get_params(), *this);
	task_blend->blend_method = get_blend_method();
	task_blend->sub_task_a() = context.build_rendering_task();
	task_blend->sub_task_b() = sub_task;
	return task_blend;
}

Context
Layer_PasteCanvas::build_context_queue(Context context, CanvasBase &out_queue)const
{
	ContextParams params(context.get_params());
	apply_z_range_to_params(params);

	if (sub_canvas)
		return sub_canvas->get_context_sorted(params, out_queue);

	out_queue.push_back(Layer::Handle());
	return Context(out_queue.begin(), params);
}
