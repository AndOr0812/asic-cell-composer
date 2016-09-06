/* Copyright (c) 2016 Fabian Schuiki */
#include "common.h"
#include "cell.h"
#include "lef.h"
#include <math.h>
#include <cairo.h>
#include <cairo-pdf.h>


static void
dump_cell_nets(cell_t *cell, FILE *out) {
	for (size_t z = 0; z < cell->nets.size; ++z) {
		net_t *net = array_at(cell->nets, net_t*, z);
		fprintf(out, "net %s {", net->name ? net->name : "<anon>");
		for (size_t z = 0; z < net->conns.size; ++z) {
			net_conn_t *conn = array_get(&net->conns, z);
			if (conn->inst) {
				fprintf(out, " %s.%s", conn->inst->name, conn->pin->name);
			} else {
				fprintf(out, " %s", conn->pin->name);
			}
		}
		fprintf(out, " }\n");
	}
}


static void
copy_geometry(geometry_t *dst, inst_t *inst, geometry_t *src) {
	assert(dst && inst && src);
	vec2_t off = vec2_sub(inst->pos, inst->cell->origin);
	for (size_t z = 0; z < src->layers.size; ++z) {
		layer_t *layer_src = array_get(&src->layers, z);
		layer_t *layer_dst = geometry_find_layer(dst, layer_src->name);

		vec2_t *raw_points = layer_src->points.items;
		vec2_t points[layer_src->points.size];
		for (size_t z = 0; z < layer_src->points.size; ++z) {
			points[z] = vec2_add(raw_points[z], off);
		}

		for (size_t z = 0; z < layer_src->shapes.size; ++z) {
			shape_t *shape = array_get(&layer_src->shapes, z);
			layer_add_shape(layer_dst, points + shape->pt_begin, shape->pt_end - shape->pt_begin);
		}
	}
}


static int
net_connects_to(net_t *net, pin_t *pin, inst_t *inst) {
	assert(net && pin);
	for (size_t z = 0; z < net->conns.size; ++z) {
		net_conn_t *conn = array_get(&net->conns, z);
		if (conn->pin == pin && conn->inst == inst)
			return 1;
	}
	return 0;
}

static void
connect(cell_t *cell, pin_t *pin_a, inst_t *inst_a, pin_t *pin_b, inst_t *inst_b) {
	assert(cell && pin_a && pin_b);

	// Find any existing nets that contain these pins. If both pins are
	// connected to the same net already, there's nothing left to do.
	net_t *net_a = NULL, *net_b = NULL;
	for (size_t z = 0; z < cell->nets.size; ++z) {
		net_t *net = array_at(cell->nets, net_t*, z);
		if (net_connects_to(net, pin_a, inst_a)) {
			assert(!net_a);
			net_a = net;
		}
		if (net_connects_to(net, pin_b, inst_b)) {
			assert(!net_b);
			net_b = net;
		}
	}
	if (net_a && net_a == net_b)
		return;

	// There are three cases to handle: 1) Two nets exist and need to be joined,
	// 2) one net exists and needs to have a pin added, or 3) no nets exist and
	// one needs to be created.
	if (!net_a && !net_b) {
		printf("creating new net\n");
		net_t *net = calloc(1, sizeof(*net));
		array_init(&net->conns, sizeof(net_conn_t));
		net_conn_t ca = { .pin = pin_a, .inst = inst_a },
		           cb = { .pin = pin_b, .inst = inst_b };
		array_add(&net->conns, &ca);
		array_add(&net->conns, &cb);
		array_add(&cell->nets, &net);
	} else if (net_a && net_b) {
		assert(0 && "not implemented");
	} else {
		if (net_a) {
			net_conn_t c = { .pin = pin_b, .inst = inst_b };
			array_add(&net_a->conns, &c);
		} else {
			net_conn_t c = { .pin = pin_a, .inst = inst_a };
			array_add(&net_b->conns, &c);
		}
	}
}


static void
plot_shape(cairo_t *cr, mat3_t M, vec2_t *points, size_t num_points, vec2_t *center) {
	vec2_t c = VEC2(0,0);
	vec2_t pt[num_points];
	for (size_t z = 0; z < num_points; ++z) {
		pt[z] = mat3_mul_vec2(M, points[z]);
		c = vec2_add(c, pt[z]);
	}

	if (num_points == 2) {
		cairo_rectangle(cr, pt[0].x, pt[0].y, pt[1].x-pt[0].x, pt[1].y-pt[0].y);
		// cairo_fill(cr);
	}

	c.x /= num_points;
	c.y /= num_points;
	if (center)
		*center = c;
}


static void
plot_layer(cairo_t *cr, mat3_t M, layer_t *layer, vec2_t *center) {
	vec2_t *points = layer->points.items;
	vec2_t c = VEC2(0,0);

	for (size_t z = 0, zn = layer->shapes.size; z < zn; ++z) {
		shape_t *shape = array_get(&layer->shapes, z);
		vec2_t tc;
		plot_shape(cr, M, points + shape->pt_begin, shape->pt_end - shape->pt_begin, &tc);
		c = vec2_add(c, tc);
	}

	c.x /= layer->shapes.size;
	c.y /= layer->shapes.size;
	if (center)
		*center = c;
}

static void
plot_cell_as_pdf(cell_t *cell, const char *filename) {
	cairo_t *cr;
	cairo_surface_t *surface;
	cairo_text_extents_t extents;

	double scale = 1e8;
	double grid = 1e-7;
	double clr_grid_maj = 0.75;
	double clr_grid_min = 0.9;

	// Calculate the extents of the cell and determine a transformation matrix
	// for all metric coordinates.
	extents_t ext = cell->ext;
	extents_add(&ext, VEC2(0,0));
	extents_add(&ext, cell_get_origin(cell));
	extents_add(&ext, cell_get_size(cell));
	vec2_t d0 = ext.min, d1 = ext.max;
	mat3_t M = mat3_scale(scale);
	M.v[1][1] *= -1; // flip along y

	vec2_t p0 = mat3_mul_vec2(M, d0);
	vec2_t p1 = mat3_mul_vec2(M, d1);
	p0.x -= 20;
	p1.x += 20;
	double tmp = p0.y;
	p0.y = p1.y;
	p1.y = tmp;
	p0.y -= 20;
	p1.y += 20;

	// Create a new PDF document that covers the entire cell extent, plus some
	// margin.
	surface = cairo_pdf_surface_create(filename, p1.x-p0.x, p1.y-p0.y);
	cr = cairo_create(surface);
	cairo_translate(cr, -p0.x, -p0.y);

	// Draw the origin lines of the grid.
	cairo_save(cr);
	vec2_t p_orig = mat3_mul_vec2(M, cell_get_origin(cell));
	cairo_move_to(cr, p0.x, 0);
	cairo_line_to(cr, p1.x, 0);
	cairo_move_to(cr, 0, p0.y);
	cairo_line_to(cr, 0, p1.y);
	cairo_new_sub_path(cr);
	cairo_arc(cr, p_orig.x, p_orig.y, 3, 0, 2*M_PI);
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgb(cr, clr_grid_maj, clr_grid_maj, clr_grid_maj);
	cairo_stroke(cr);

	// Draw the grid.
	for (double f = floor(d0.x/grid)*grid; f <= d1.x; f += grid) {
		vec2_t gp0 = mat3_mul_vec2(M, VEC2(f,d0.y));
		vec2_t gp1 = mat3_mul_vec2(M, VEC2(f,d1.y));
		cairo_move_to(cr, gp0.x, gp0.y+5);
		cairo_line_to(cr, gp1.x, gp1.y-5);
	}
	for (double f = floor(d0.y/grid)*grid; f <= d1.y; f += grid) {
		vec2_t gp0 = mat3_mul_vec2(M, VEC2(d0.x,f));
		vec2_t gp1 = mat3_mul_vec2(M, VEC2(d1.x,f));
		cairo_move_to(cr, gp0.x-5, gp0.y);
		cairo_line_to(cr, gp1.x+5, gp1.y);
	}
	cairo_set_line_width(cr, 0.5);
	cairo_set_source_rgb(cr, clr_grid_min, clr_grid_min, clr_grid_min);
	cairo_stroke(cr);

	// Draw the cell origin and size.
	vec2_t box0 = mat3_mul_vec2(M, VEC2(0,0));
	vec2_t box1 = mat3_mul_vec2(M, cell_get_size(cell));
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_dash(cr, (double[]){3.0, 2.0}, 2, 0);
	cairo_rectangle(cr, box0.x, box0.y, box1.x-box0.x, box1.y-box0.y);
	cairo_stroke(cr);
	cairo_restore(cr);

	// Draw the cell name.
	cairo_move_to(cr, p0.x+15, p0.y+15);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_show_text(cr, cell_get_name(cell));

	// Draw the instances in the cell.
	cairo_save(cr);
	cairo_set_line_width(cr, 0.5);
	for (size_t z = 0, zn = cell_get_num_insts(cell); z < zn; ++z) {
		inst_t *inst = cell_get_inst(cell, z);
		cell_t *subcell = inst_get_cell(inst);
		vec2_t box0 = mat3_mul_vec2(M, inst_get_pos(inst));
		vec2_t box1 = mat3_mul_vec2(M, vec2_add(inst_get_pos(inst), cell_get_size(subcell)));
		cairo_set_source_rgb(cr, 0, 0, 1);
		cairo_rectangle(cr, box0.x, box0.y, box1.x-box0.x, box1.y-box0.y);
		cairo_move_to(cr, box0.x, box1.y);
		cairo_line_to(cr, box1.x, box0.y);
		cairo_move_to(cr, box0.x, box0.y);
		cairo_line_to(cr, box1.x, box1.y);
		cairo_text_extents(cr, cell_get_name(subcell), &extents);
		cairo_move_to(cr, (box0.x+box1.x-extents.width)/2, (box0.y+box1.y+extents.height)/2);
		cairo_show_text(cr, cell_get_name(subcell));
		cairo_stroke(cr);
	}
	cairo_restore(cr);

	// Draw the cell geometry.
	cairo_set_line_width(cr, 0.5);
	cairo_save(cr);
	for (size_t z = 0, zn = cell->geo.layers.size; z < zn; ++z) {
		cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
		plot_layer(cr, M, array_get(&cell->geo.layers, z), NULL);
		cairo_fill(cr);
	}
	cairo_restore(cr);

	// Draw the cell pins.
	cairo_save(cr);
	for (size_t z = 0, zn = cell->pins.size; z < zn; ++z) {
		pin_t *pin = array_at(cell->pins, pin_t*, z);
		const char *name = pin->name;
		for (size_t z = 0, zn = pin->geo.layers.size; z < zn; ++z) {
			vec2_t c;
			cairo_set_source_rgb(cr, 1, 0, 0);
			plot_layer(cr, M, array_get(&pin->geo.layers, z), &c);
			cairo_stroke(cr);

			cairo_set_source_rgb(cr, 0, 0, 0);
			cairo_text_extents(cr, name, &extents);
			cairo_move_to(cr, c.x-extents.width/2, c.y+extents.height/2);
			cairo_show_text(cr, name);
			cairo_stroke(cr);
		}
	}
	cairo_restore(cr);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}


int
main(int argc, char **argv) {
	int res;
	int i;

	// Create a new library into which cells shall be laoded.
	library_t *lib = new_library();

	for (i = 1; i < argc; i++) {
		lef_t *lef;
		res = read_lef_file(argv[i], &lef);
		if (res != PHALANX_OK) {
			printf("Unable to read LEF file %s: %s\n", argv[i], errstr(res));
			return 1;
		}

		// Convert each macro to a cell.
		for (size_t z = 0, zn = lef_get_num_macros(lef); z < zn; ++z) {
			lef_macro_t *macro = lef_get_macro(lef,z);
			cell_t *cell = new_cell(lib, lef_macro_get_name(macro));
			lef_xy_t xy = lef_macro_get_size(macro);
			cell_set_size(cell, VEC2(xy.x*1e-6, xy.y*1e-6));

			for (size_t y = 0, yn = lef_macro_get_num_pins(macro); y < yn; ++y) {
				lef_pin_t *pin = lef_macro_get_pin(macro, y);
				pin_t *cell_pin = cell_find_pin(cell, lef_pin_get_name(pin));
				geometry_t *pin_geo = &cell_pin->geo;

				for (size_t x = 0, xn = lef_pin_get_num_ports(pin); x < xn; ++x) {
					lef_port_t *port = lef_pin_get_port(pin, x);

					for (size_t w = 0, wn = lef_port_get_num_geos(port); w < wn; ++w) {
						lef_geo_t *geo = lef_port_get_geo(port, w);
						if (geo->kind == LEF_GEO_LAYER) {
							lef_geo_layer_t *layer = (void*)geo;
							layer_t *pin_layer = geometry_find_layer(pin_geo, lef_geo_layer_get_name(layer));

							for (size_t v = 0, vn = lef_geo_layer_get_num_shapes(layer); v < vn; ++v) {
								lef_geo_shape_t *shape = lef_geo_layer_get_shape(layer, v);
								uint32_t num_points = lef_geo_shape_get_num_points(shape);
								lef_xy_t *points = lef_geo_shape_get_points(shape);
								vec2_t scaled[num_points];
								for (unsigned i = 0; i < num_points; ++i) {
									scaled[i].x = points[i].x * 1e-6;
									scaled[i].y = points[i].y * 1e-6;
								}
								layer_add_shape(pin_layer, scaled, num_points);
								/// @todo Consider the shape's step pattern and replicate the geometry accordingly.
							}
						}
						/// @todo Add support for the VIA geometry.
					}
				}
			}

			cell_update_extents(cell);
		}

		// do something with the LEF file
		printf("Read %u macros\n", (uint32_t)lef_get_num_macros(lef));
		lef_free(lef);
	}

	// Create a new cell.
	cell_t *cell = new_cell(lib, "AND4");
	cell_t *AN2M0R = get_cell(lib, "AN2M0R");
	assert(AN2M0R);
	vec2_t AN2M0R_sz = cell_get_size(AN2M0R);

	inst_t *i0 = new_inst(cell, AN2M0R, "I0");
	inst_t *i1 = new_inst(cell, AN2M0R, "I1");
	inst_t *i2 = new_inst(cell, AN2M0R, "I2");

	// Place the AND gates.
	vec2_t p = {0,0};
	inst_set_pos(i0, p);
	p.x += AN2M0R_sz.x;
	inst_set_pos(i1, p);
	p.x += AN2M0R_sz.x;
	inst_set_pos(i2, p);
	p.x += AN2M0R_sz.x;
	p.y += AN2M0R_sz.y;
	cell_set_size(cell, p);
	// cell_set_origin(cell, VEC2(-0.2e-6, -0.2e-6));

	// Add the pins.
	pin_t *pA   = cell_find_pin(cell, "A"),
	      *pB   = cell_find_pin(cell, "B"),
	      *pC   = cell_find_pin(cell, "C"),
	      *pD   = cell_find_pin(cell, "D"),
	      *pZ   = cell_find_pin(cell, "Z"),
	      *pVDD = cell_find_pin(cell, "VDD"),
	      *pVSS = cell_find_pin(cell, "VSS");

	copy_geometry(&pA->geo, i0, &cell_find_pin(AN2M0R, "A")->geo);
	copy_geometry(&pB->geo, i0, &cell_find_pin(AN2M0R, "B")->geo);
	copy_geometry(&pC->geo, i1, &cell_find_pin(AN2M0R, "A")->geo);
	copy_geometry(&pD->geo, i1, &cell_find_pin(AN2M0R, "B")->geo);
	copy_geometry(&pZ->geo, i2, &cell_find_pin(AN2M0R, "Z")->geo);

	layer_add_shape(geometry_find_layer(&pVDD->geo, "ME1"), (vec2_t[]){
		{0, 1.65e-6}, {p.x, 1.95e-6}
	}, 2);
	layer_add_shape(geometry_find_layer(&pVSS->geo, "ME1"), (vec2_t[]){
		{0, -0.15e-6}, {p.x, 0.15e-6}
	}, 2);

	// Add the internal connections of the cell.
	connect(cell, pVDD, NULL, cell_find_pin(AN2M0R, "VDD"), i0);
	connect(cell, pVDD, NULL, cell_find_pin(AN2M0R, "VDD"), i1);
	connect(cell, pVDD, NULL, cell_find_pin(AN2M0R, "VDD"), i2);

	connect(cell, pVSS, NULL, cell_find_pin(AN2M0R, "VSS"), i0);
	connect(cell, pVSS, NULL, cell_find_pin(AN2M0R, "VSS"), i1);
	connect(cell, pVSS, NULL, cell_find_pin(AN2M0R, "VSS"), i2);

	connect(cell, pA, NULL, cell_find_pin(AN2M0R, "A"), i0);
	connect(cell, pB, NULL, cell_find_pin(AN2M0R, "B"), i0);
	connect(cell, pC, NULL, cell_find_pin(AN2M0R, "A"), i1);
	connect(cell, pD, NULL, cell_find_pin(AN2M0R, "B"), i1);
	connect(cell, pZ, NULL, cell_find_pin(AN2M0R, "Z"), i2);
	connect(cell, cell_find_pin(AN2M0R, "Z"), i0, cell_find_pin(AN2M0R, "A"), i2);
	connect(cell, cell_find_pin(AN2M0R, "Z"), i1, cell_find_pin(AN2M0R, "B"), i2);

	/// @todo Do something with this new cell...
	cell_update_extents(cell);
	plot_cell_as_pdf(AN2M0R, "debug_AN2M0R.pdf");
	plot_cell_as_pdf(cell, "debug.pdf");

	// Dump nets in the cell.
	dump_cell_nets(cell, stdout);

	// Clean up.
	free_library(lib);
	return 0;
}
