/* Copyright (c) 2016 Fabian Schuiki */
#pragma once
#include "common.h"
#include "util.h"


/**
 * Bits of a design that can be invalid.
 */
enum {
	PHX_EXTENTS      = 1 << 0,
	PHX_CAPACITANCES = 1 << 1,
	PHX_TIMING       = 1 << 2,
	PHX_POWER_LKG    = 1 << 3,
	PHX_POWER_INT    = 1 << 4,
	PHX_ALL_BITS     = 0x1F,
	PHX_INIT_INVALID = PHX_ALL_BITS,
};

struct phx_extents {
	vec2_t min;
	vec2_t max;
};

struct phx_library {
	/// The technology the cells in this library are implemented in.
	phx_tech_t *tech;
	/// The cells in this library.
	array_t cells; /* phx_cell_t* */
};

struct phx_geometry {
	/// The bits of this geometry that need to be recalculated.
	uint8_t invalid;
	/// The cell that contains this geometry.
	phx_cell_t *cell;
	/// The layers this geometry contains information for.
	array_t layers; /* phx_layer_t */
	/// The extents of the geometry.
	phx_extents_t ext;
};

struct phx_layer {
	/// The bits of this layer that need to be recalculated.
	uint8_t invalid;
	/// The geometry that contains this layer.
	phx_geometry_t *geo;
	/// The technology layer this layer corresponds to.
	phx_tech_layer_t *tech;
	/// The lines on this layer.
	array_t lines; /* phx_line_t* */
	/// The shapes on this layer.
	array_t shapes; /* phx_shape_t* */
	/// The layer's extents.
	phx_extents_t ext;
};

struct phx_line {
	/// The width of the line, in meters.
	double width;
	/// The number of points in the line. Must be at least 2.
	uint16_t num_pts;
	/// The points in the line.
	vec2_t pts[];
};

struct phx_shape {
	/// Number of points in the shape.
	uint16_t num_pts;
	/// The points in the shape.
	vec2_t pts[];
};

struct phx_pin {
	phx_cell_t *cell;
	char *name;
	phx_geometry_t geo;
	double capacitance;
};

struct phx_cell {
	/// The bits of this cell that need to be recalculated.
	uint8_t invalid;
	/// The library this cell is part of.
	phx_library_t *lib;
	/// The cell's name.
	char *name;
	/// The cell's origin, in meters.
	vec2_t origin;
	/// The cell's size, in meters.
	vec2_t size;
	/// Instances contained within this cell.
	array_t insts;
	/// The cell's extents.
	phx_extents_t ext;
	/// The cell's geometry.
	phx_geometry_t geo;
	/// The cell's pins.
	array_t pins; /* phx_pin_t* */
	/// The cell's nets.
	array_t nets; /* phx_net_t* */
	/// The cell's timing arcs.
	array_t arcs; /* phx_timing_arc_t */
	/// The cell's geometry as loaded from a GDS file.
	gds_struct_t *gds;
	/// The leakage power the cell dissipates.
	double leakage_power;
	/// Instantiations of this cell.
	ptrset_t uses;
	/// Manually created GDS text elements.
	array_t gds_text;
};

enum phx_orientation {
	/// Invert the X axis.
	PHX_MIRROR_X   = 1 << 0,
	/// Invert the Y axis.
	PHX_MIRROR_Y   = 1 << 1,
	/// Rotate clockwise by 90 degrees.
	PHX_ROTATE_90  = 1 << 2,
	PHX_ROTATE_180 = PHX_MIRROR_X | PHX_MIRROR_Y,
	PHX_ROTATE_270 = PHX_ROTATE_90 | PHX_ROTATE_180,
};

struct phx_inst {
	/// Invalidated bits of the instance.
	uint8_t invalid;
	/// The instantiated cell.
	phx_cell_t *cell;
	/// The cell within which this instance is placed.
	phx_cell_t *parent;
	/// The instance's orientation.
	uint8_t orientation; /* enum phx_orientation */
	/// The instance name.
	char *name;
	/// The position of the cell's origin.
	vec2_t pos;
	/// The instance's extents.
	phx_extents_t ext;
};

struct phx_terminal {
	phx_inst_t *inst;
	phx_pin_t *pin;
};

enum phx_timing_type {
	PHX_TIM_DELAY,
	PHX_TIM_TRANS,
};

struct phx_timing_arc {
	phx_pin_t *pin;
	phx_pin_t *related_pin;
	phx_table_t *delay;
	phx_table_t *transition;
};

struct phx_gds_text {
	unsigned layer;
	unsigned type;
	vec2_t pos;
	char *text;
};


void phx_extents_reset(phx_extents_t*);
void phx_extents_include(phx_extents_t*, phx_extents_t*);
void phx_extents_add(phx_extents_t*, vec2_t);

/* Library */
phx_library_t *phx_library_create(phx_tech_t*);
void phx_library_destroy(phx_library_t*);
phx_cell_t *phx_library_find_cell(phx_library_t*, const char*, bool);

/* Cell */
phx_cell_t *new_cell(phx_library_t*, const char *name);
void free_cell(phx_cell_t*);
const char *phx_cell_get_name(phx_cell_t*);
void phx_cell_set_origin(phx_cell_t*, vec2_t);
void phx_cell_set_size(phx_cell_t*, vec2_t);
vec2_t phx_cell_get_origin(phx_cell_t*);
vec2_t phx_cell_get_size(phx_cell_t*);
size_t phx_cell_get_num_insts(phx_cell_t*);
phx_inst_t *phx_cell_get_inst(phx_cell_t*, size_t idx);
phx_inst_t *phx_cell_find_inst(phx_cell_t*, const char*);
phx_geometry_t *phx_cell_get_geometry(phx_cell_t*);
phx_pin_t *cell_find_pin(phx_cell_t*, const char *name);
void phx_cell_set_gds(phx_cell_t *cell, gds_struct_t *gds);
gds_struct_t *phx_cell_get_gds(phx_cell_t *cell);
unsigned phx_cell_get_num_pins(phx_cell_t*);
phx_pin_t *phx_cell_get_pin(phx_cell_t*, unsigned);
void phx_cell_set_timing_table(phx_cell_t*, phx_pin_t*, phx_pin_t*, phx_timing_type_t, phx_table_t*);
void phx_cell_update(phx_cell_t*, uint8_t);
double phx_cell_get_leakage_power(phx_cell_t*);
void phx_cell_add_gds_text(phx_cell_t*, unsigned, unsigned, vec2_t, const char*);

/* Pin */
const char *phx_pin_get_name(phx_pin_t*);
phx_geometry_t *phx_pin_get_geometry(phx_pin_t*);
void phx_pin_set_capacitance(phx_pin_t*, double);
double phx_pin_get_capacitance(phx_pin_t*);

/* Geometry */
void phx_geometry_init(phx_geometry_t*, phx_cell_t*);
void phx_geometry_dispose(phx_geometry_t*);
phx_layer_t *phx_geometry_on_layer(phx_geometry_t*, phx_tech_layer_t*);
unsigned phx_geometry_get_num_layers(phx_geometry_t*);
phx_layer_t *phx_geometry_get_layer(phx_geometry_t*, unsigned);
void phx_geometry_update(phx_geometry_t*, uint8_t);

/* Layer */
void phx_layer_init(phx_layer_t*, phx_geometry_t*, phx_tech_layer_t*);
void phx_layer_dispose(phx_layer_t*);
phx_line_t *phx_layer_add_line(phx_layer_t*, double, size_t, vec2_t*);
phx_shape_t *phx_layer_add_shape(phx_layer_t*, size_t, vec2_t*);
size_t phx_layer_get_num_lines(phx_layer_t*);
size_t phx_layer_get_num_shapes(phx_layer_t*);
phx_line_t *phx_layer_get_line(phx_layer_t*, size_t);
phx_shape_t *phx_layer_get_shape(phx_layer_t*, size_t);
void phx_layer_update(phx_layer_t*, uint8_t);
phx_tech_layer_t *phx_layer_get_tech(phx_layer_t*);

/* Instance */
phx_inst_t *new_inst(phx_cell_t *into, phx_cell_t *cell, const char *name);
void free_inst(phx_inst_t*);
void inst_set_pos(phx_inst_t*, vec2_t);
vec2_t inst_get_pos(phx_inst_t*);
phx_cell_t *inst_get_cell(phx_inst_t*);
void inst_update_extents(phx_inst_t*);
void phx_inst_set_orientation(phx_inst_t*, phx_orientation_t);
phx_orientation_t phx_inst_get_orientation(phx_inst_t*);
vec2_t phx_inst_vec_from_parent(phx_inst_t*, vec2_t);
vec2_t phx_inst_vec_to_parent(phx_inst_t*, vec2_t);
void phx_inst_copy_geometry_to_parent(phx_inst_t*, phx_geometry_t*, phx_geometry_t*);
void phx_inst_update(phx_inst_t*, uint8_t);


/**
 * @defgroup net Net
 * @{
 */
struct phx_net {
	/// Invalidated bits of the net.
	uint8_t invalid;
	/// The cell this net belongs to.
	phx_cell_t *cell;
	/// The net's name.
	char *name;
	/// The connections this net makes.
	array_t conns; /* phx_terminal_t */
	/// The capacitance of this net, including attached pins.
	double capacitance;
	/// The timing arcs to this net.
	array_t arcs; /* phx_timing_arc_t */
	/// Whether this net is visible outside the cell through a pin.
	int is_exposed;
};

void phx_net_update(phx_net_t*, uint8_t);
/** @} */
