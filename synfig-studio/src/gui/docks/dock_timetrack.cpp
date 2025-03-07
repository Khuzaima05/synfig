/* === S Y N F I G ========================================================= */
/*!	\file dock_timetrack.cpp
**	\brief Template File
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**  Copyright (c) 2010 Carlos López
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

#include <cassert>

#include <gtkmm/scrolledwindow.h>

#include <synfig/general.h>
#include <synfig/timepointcollect.h>

#include <helpers.h>
#include <app.h>
#include <instance.h>
#include <canvasview.h>
#include <workarea.h>
#include <trees/layertree.h>
#include <trees/layerparamtreestore.h>
#include <widgets/widget_canvastimeslider.h>
#include <widgets/widget_keyframe_list.h>

#include "dock_timetrack.h"

#include <gui/localization.h>

#endif

/* === U S I N G =========================================================== */

using namespace std;
using namespace etl;
using namespace synfig;
using namespace studio;

/* === M A C R O S ========================================================= */

/* === C L A S S E S ======================================================= */

class TimeTrackView : public Gtk::TreeView
{
private:
	sigc::connection expand_connection;
	sigc::connection collapse_connection;
	
	CellRenderer_TimeTrack *cellrenderer_time_track;

public:
	sigc::signal<void,synfigapp::ValueDesc,std::set<synfig::Waypoint, std::less<UniqueID> >,int> signal_waypoint_clicked_timetrackview;

	LayerParamTreeStore::Model model;

	void set_canvas_view(handle<CanvasView> canvas_view)
	{
		cellrenderer_time_track->set_time_model(canvas_view->time_model());
		cellrenderer_time_track->set_canvas_interface(canvas_view->canvas_interface());
	}

	TimeTrackView()
	{
		//Text attributes must be the same that layer param tree's to have aligned rows
		Pango::AttrList attr_list;
		{
			Pango::AttrInt pango_size(Pango::Attribute::create_attr_size(Pango::SCALE*8));
			pango_size.set_start_index(0);
			pango_size.set_end_index(64);
			attr_list.change(pango_size);
		}

		int label_index(append_column_editable(_("Name"),model.label));
		Gtk::TreeView::Column* label_column = get_column(label_index-1);
		label_column->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);

		{	// --- T I M E   T R A C K --------------------------------------------
			Gtk::TreeView::Column* column = Gtk::manage( new Gtk::TreeView::Column(_("Time Track")) );
			Gtk::TreeView::Column* column2 = Gtk::manage( new Gtk::TreeView::Column("Align") );

			// Set up the value-node cell-renderer
			cellrenderer_time_track=LayerParamTreeStore::add_cell_renderer_value_node(column);
			cellrenderer_time_track->property_mode()=Gtk::CELL_RENDERER_MODE_ACTIVATABLE;
			cellrenderer_time_track->signal_waypoint_clicked_cellrenderer().connect(sigc::mem_fun(*this, &TimeTrackView::on_waypoint_clicked_timetrackview));
			cellrenderer_time_track->signal_waypoint_changed().connect(sigc::mem_fun(*this, &TimeTrackView::on_waypoint_changed) );
			//column->add_attribute(cellrenderer_time_track->property_value_desc(), model.value_desc);
			//column->add_attribute(cellrenderer_time_track->property_canvas(), model.canvas);
			//column->add_attribute(cellrenderer_time_track->property_visible(), model.is_value_node);

			//column->pack_start(*cellrenderer_time_track);

			// Add a fixed size (same that layer param tree) empty text renderer
			// to align the rows with params dock when the text is taller than value_type icons height
			Gtk::CellRendererText* text_cellrenderer = Gtk::manage( new Gtk::CellRendererText() );
			text_cellrenderer->property_attributes()=attr_list;
			column2->pack_end(*text_cellrenderer,false);
			text_cellrenderer->set_fixed_size (1,-1);

			// Add a fixed size (1pixel width, same height than value_type icon) empty (alpha) icon
			// to align the rows with params dock when the text is smaller than value_type icons height
			Gtk::CellRendererPixbuf* icon_cellrenderer = Gtk::manage( new Gtk::CellRendererPixbuf() );
			Glib::RefPtr<Gdk::Pixbuf> pixbuf;
			pixbuf=Gtk::Button().render_icon_pixbuf(Gtk::StockID("synfig-utils_timetrack_align"),Gtk::ICON_SIZE_SMALL_TOOLBAR);
			icon_cellrenderer->property_pixbuf() = pixbuf;
			column2->pack_end(*icon_cellrenderer,false);
			icon_cellrenderer->set_fixed_size(1,-1);

			// Finish setting up the columns
			column->set_reorderable();
			column->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);
			column->set_resizable();
			column->set_expand(true);
// Commented during Align rows fixing
// https://www.synfig.org/issues/thebuggenie/synfig/issues/161
// 			column->set_min_width(200);
			
			column2->set_resizable();
			column2->set_fixed_width(1);

			append_column(*column);
			append_column(*column2);
		}
		set_rules_hint();

		set_expander_column(*label_column);
		label_column->set_visible(false);
		set_headers_visible(false);
		set_size_request(-1,64);
	}


	bool
	on_event(GdkEvent *event)
	{
		switch(event->type)
		{
		case GDK_BUTTON_PRESS:
			{
				Gtk::TreeModel::Path path;
				Gtk::TreeViewColumn *column;
				int cell_x, cell_y;
				if(!get_path_at_pos(
					int(event->button.x),int(event->button.y),	// x, y
					path, // TreeModel::Path&
					column, //TreeViewColumn*&
					cell_x,cell_y //int&cell_x,int&cell_y
					)
				) break;
				const Gtk::TreeRow row = *(get_model()->get_iter(path));

				if(column && column->get_first_cell()==cellrenderer_time_track)
				{
					Gdk::Rectangle rect;
					get_cell_area(path,*column,rect);
					cellrenderer_time_track->property_value_desc()=row[model.value_desc];
					cellrenderer_time_track->property_canvas()=row[model.canvas];
					cellrenderer_time_track->activate(event,*this,path.to_string(),rect,rect,Gtk::CellRendererState());
					queue_draw_area(rect.get_x(),rect.get_y(),rect.get_width(),rect.get_height());
					return true;
					//return signal_param_user_click()(event->button.button,row,COLUMNID_TIME_TRACK);
				}
			}
			break;
		case GDK_MOTION_NOTIFY:
			{
				Gtk::TreeModel::Path path;
				Gtk::TreeViewColumn *column;
				int cell_x, cell_y;
				if(!get_path_at_pos(
					(int)event->motion.x,(int)event->motion.y,	// x, y
					path, // TreeModel::Path&
					column, //TreeViewColumn*&
					cell_x,cell_y //int&cell_x,int&cell_y
					)
				) break;

				if(!get_model()->get_iter(path))
					break;

				Gtk::TreeRow row = *(get_model()->get_iter(path));

				if (((event->motion.state&GDK_BUTTON1_MASK) || (event->motion.state&GDK_BUTTON3_MASK)) &&
					column &&
					cellrenderer_time_track == column->get_first_cell())
				{
					Gdk::Rectangle rect;
					get_cell_area(path,*column,rect);
					cellrenderer_time_track->property_value_desc()=row[model.value_desc];
					cellrenderer_time_track->property_canvas()=row[model.canvas];
					cellrenderer_time_track->activate(event,*this,path.to_string(),rect,rect,Gtk::CellRendererState());
					queue_draw();
					//queue_draw_area(rect.get_x(),rect.get_y(),rect.get_width(),rect.get_height());
					return true;
				}
				return true;
			}
			break;
		case GDK_BUTTON_RELEASE:
			{
				Gtk::TreeModel::Path path;
				Gtk::TreeViewColumn *column;
				int cell_x, cell_y;
				if(!get_path_at_pos(
					(int)event->button.x,(int)event->button.y,	// x, y
					path, // TreeModel::Path&
					column, //TreeViewColumn*&
					cell_x,cell_y //int&cell_x,int&cell_y
					)
				) break;

				if(!get_model()->get_iter(path))
					break;

				Gtk::TreeRow row = *(get_model()->get_iter(path));

				if(column && cellrenderer_time_track==column->get_first_cell())
				{
					Gdk::Rectangle rect;
					get_cell_area(path,*column,rect);
					cellrenderer_time_track->property_value_desc()=row[model.value_desc];
					cellrenderer_time_track->property_canvas()=row[model.canvas];
					cellrenderer_time_track->activate(event,*this,path.to_string(),rect,rect,Gtk::CellRendererState());
					queue_draw();
					//queue_draw_area(rect.get_x(),rect.get_y(),rect.get_width(),rect.get_height());
					return true;
				}
			}
			break;
		default:
			break;
		}
		return Gtk::TreeView::on_event(event);
	}

	void set_model(Glib::RefPtr<LayerParamTreeStore> store)
	{
		Gtk::TreeView::set_model(store);
		cellrenderer_time_track->set_canvas_interface( store->canvas_interface() );
	}

	void
	on_waypoint_changed( synfig::Waypoint waypoint , synfig::ValueNode::Handle value_node)
	{
		// \todo is this code used?
		assert(0);

		Glib::RefPtr<LayerParamTreeStore> store =
			Glib::RefPtr<LayerParamTreeStore>::cast_dynamic( get_model() );
		assert(store);
		
		synfigapp::Action::ParamList param_list;
		param_list.add("canvas", store->canvas_interface()->get_canvas());
		param_list.add("canvas_interface", store->canvas_interface());
		param_list.add("value_node", value_node);
		param_list.add("waypoint", waypoint);
	//	param_list.add("time", canvas_interface()->get_time());

		etl::handle<studio::Instance>::cast_static(store->canvas_interface()->get_instance())->process_action("WaypointSetSmart", param_list);
	}

	void mimic(Gtk::TreeView *tree_view)
	{
		expand_connection.disconnect();
		collapse_connection.disconnect();
		
		expand_connection = tree_view->signal_row_expanded().connect(
			sigc::hide<0>(
			sigc::hide_return(
				sigc::bind<-1>(
					sigc::mem_fun(
						*this,
						&Gtk::TreeView::expand_row
					),
					false
				)
			))
		);
		collapse_connection = tree_view->signal_row_collapsed().connect(
			sigc::hide<0>(
			sigc::hide_return(
					sigc::mem_fun(
						*this,
						&Gtk::TreeView::collapse_row
					)
			))
		);
	}

	void
	on_waypoint_clicked_timetrackview(const etl::handle<synfig::Node>& node,
									  const synfig::Time& time,
									  const synfig::Time& /*time_offset*/,
									  const synfig::Time& /*time_dilation*/,
									  int button)
	{
		std::set<synfig::Waypoint, std::less<UniqueID> > waypoint_set;
		synfig::waypoint_collect(waypoint_set,time,node);

		synfigapp::ValueDesc value_desc;

		if (waypoint_set.size() == 1)
		{
			ValueNode::Handle value_node(waypoint_set.begin()->get_parent_value_node());
			assert(value_node);

			Glib::RefPtr<LayerParamTreeStore> store =
				Glib::RefPtr<LayerParamTreeStore>::cast_dynamic( get_model() );
			assert(store);
			
			Gtk::TreeRow row;
			if (store && store->find_first_value_node(value_node, row) && row)
				value_desc = static_cast<synfigapp::ValueDesc>(row[model.value_desc]);
		}

		if (!waypoint_set.empty())
			signal_waypoint_clicked_timetrackview(value_desc,waypoint_set,button);
	}
};

/* === G L O B A L S ======================================================= */

/* === P R O C E D U R E S ================================================= */

/* === M E T H O D S ======================================================= */

Dock_Timetrack::Dock_Timetrack():
	Dock_CanvasSpecific("timetrack",_("Timetrack"),Gtk::StockID("synfig-timetrack")),
	grid_()
{
	set_use_scrolled(false);
}

Dock_Timetrack::~Dock_Timetrack()
{
	if (grid_) delete grid_;
}

void
Dock_Timetrack::init_canvas_view_vfunc(etl::loose_handle<CanvasView> canvas_view)
{
	LayerParamTreeStore::Model model;

	Glib::RefPtr<LayerParamTreeStore> tree_store = Glib::RefPtr<LayerParamTreeStore>::cast_dynamic( canvas_view->get_tree_model("params") );
	Gtk::TreeView *mimic_tree_view = dynamic_cast<Gtk::TreeView*>(canvas_view->get_ext_widget("params"));
	assert(mimic_tree_view);

	TimeTrackView* tree_view = new TimeTrackView();
	tree_view->set_canvas_view(canvas_view);
	tree_view->set_model(tree_store);
	tree_view->mimic(mimic_tree_view);
	tree_view->signal_waypoint_clicked_timetrackview.connect(
		sigc::mem_fun(*canvas_view, &studio::CanvasView::on_waypoint_clicked_canvasview) );
	canvas_view->time_model()->signal_changed().connect(
		sigc::mem_fun(*tree_view,&Gtk::TreeView::queue_draw) );
	canvas_view->set_ext_widget(get_name(), tree_view);
	tree_view->show();

	canvas_view->get_adjustment_group("params")->add(vscrollbar_.get_adjustment());
	
	studio::LayerTree *tree_layer = dynamic_cast<studio::LayerTree*>(canvas_view->get_ext_widget("layers_cmp") );
	assert(tree_layer);
	tree_layer->signal_param_tree_header_height_changed().connect(
		sigc::mem_fun(*this, &studio::Dock_Timetrack::on_update_header_height) );
}

void
Dock_Timetrack::changed_canvas_view_vfunc(etl::loose_handle<CanvasView> canvas_view)
{
	if (grid_)
	{
		hscrollbar_.unset_adjustment();

		widget_timeslider_.set_canvas_view( CanvasView::Handle() );

		widget_kf_list_.set_time_model( etl::handle<TimeModel>() );
		widget_kf_list_.set_canvas_interface( etl::loose_handle<synfigapp::CanvasInterface>() );

		delete grid_;
		grid_=0;
	}
	
	if (canvas_view)
	{
		TimeTrackView *tree_view = dynamic_cast<TimeTrackView*>(canvas_view->get_ext_widget(get_name()));
		assert(tree_view);

		vscrollbar_.get_adjustment()->set_value(0);
		tree_view->show();
		
		Gtk::ScrolledWindow* scrolled = Gtk::manage(new Gtk::ScrolledWindow);
		scrolled->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
		scrolled->add(*tree_view);
		scrolled->set_vadjustment(vscrollbar_.get_adjustment());
		scrolled->get_vscrollbar()->hide();
		scrolled->show();

		// Fixed size drawing areas to align the widget_timeslider and tree_view time cursors
		// TODO ?: one align_drawingArea.(0, 1, 0, 1) modify_bg KF's color another (0, 1, 1, 2) modify_bg TS's color
		Gtk::DrawingArea* align_drawingArea1 = Gtk::manage(new Gtk::DrawingArea);
		// TODO ?: one align_drawingArea.(2, 3, 0, 1) modify_bg KF's color another (2, 3, 1, 2) modify_bg TS's color
		Gtk::DrawingArea* align_drawingArea2 = Gtk::manage(new Gtk::DrawingArea);
#if GTKMM_MAJOR_VERSION < 3 || (GTKMM_MAJOR_VERSION == 3 && GTKMM_MINOR_VERSION < 14)
		align_drawingArea1->set_size_request(4,-1);
		align_drawingArea2->set_size_request(9,-1);
#else
		align_drawingArea1->set_size_request(2,-1);
		align_drawingArea2->set_size_request(4,-1);
#endif
		align_drawingArea1->show();
		align_drawingArea2->show();

		widget_timeslider_.set_canvas_view(canvas_view);
		widget_timeslider_.show();

		widget_kf_list_.set_time_model(canvas_view->time_model());
		widget_kf_list_.set_canvas_interface(canvas_view->canvas_interface());
		widget_kf_list_.show();

		hscrollbar_.set_adjustment(canvas_view->time_model()->scroll_time_adjustment());
		vscrollbar_.show();
		hscrollbar_.show();

		//  0------1------2------3------4
		//  |  A   |  KF  |  A   |  v   |
		//  |  L   |      |  L   |  s   |
		//  1  I   x------x  I   x  c   x
		//  |  G   |  TS  |  G   |  r   |
		//  |  N1  |      |  N2  |  o   |
		//  2------x------x------x  l   x
		//  |  tree_view         |  l   |
		//  |                    |  b   |
		//  3------x------x------x------x
		//  | hscrollbar                |
		//  |                           |
		//  4------x------x------x------x
		//
		// KF = widget_kf_list
		// TS = widget_timeslider
		// ALIGN1 = align_drawingArea1
		// ALIGN2 = align_drawingArea2

		widget_kf_list_.set_hexpand();
		widget_timeslider_.set_hexpand();
		scrolled->set_hexpand();
		scrolled->set_vexpand();
		vscrollbar_.set_vexpand();
		hscrollbar_.set_hexpand();
		
		grid_=new Gtk::Grid();
		grid_->set_column_homogeneous(false);
		grid_->set_row_homogeneous(false);
		grid_->attach(*align_drawingArea1, 0, 0, 1, 2);
		grid_->attach(widget_kf_list_,     1, 0, 1, 1);
		grid_->attach(widget_timeslider_,  1, 1, 1, 1);
		grid_->attach(*align_drawingArea2, 2, 0, 1, 2);
		grid_->attach(*scrolled,           0, 2, 3, 1);
		grid_->attach(vscrollbar_,         3, 0, 1, 3);
		grid_->attach(hscrollbar_,         0, 3, 4, 1);
		grid_->show();
		add(*grid_);
	}
}

void
Dock_Timetrack::on_update_header_height(int height)
{
	int w = 0, h = 0;
	widget_kf_list_.get_size_request(w, h);
	int ts_height = std::max(1, height - h);

	widget_timeslider_.get_size_request(w, h);
	if (h != ts_height)
		widget_timeslider_.set_size_request(-1, ts_height);
}
