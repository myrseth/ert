#include <plot.h>
#include <plot_dataset.h>


/**
 * @brief Contains information about a dataset.
 */
struct plot_dataset_struct {
    double *xvalue; /**< Vector containing x-axis data */
    double *yvalue; /**< Vector containing y-axis data */
    double std_y;
    int length;	/**< Length of the vectors defining the axis */
    plot_style_type style; /**< The graph style */
    plot_color_type color; /**< The graph color */
    int step;
    bool finished;
};

void plot_dataset_finished(plot_dataset_type * d, bool flag)
{
    if (!d)
	return;

    d->finished = flag;
}

bool plot_dataset_is_finished(plot_dataset_type * d)
{
    if (d->finished)
	return true;

    return false;
}

int plot_dataset_get_step(plot_dataset_type * d)
{
    if (!d)
	return -1;

    return d->step;
}

int plot_dataset_step_next(plot_dataset_type * d)
{
    if (!d)
	return -1;

    d->step++;
    return d->step;
}

int plot_datset_get_length(plot_dataset_type * d)
{
    if (!d)
	return -1;

    return d->length;
}

plot_color_type plot_datset_get_color(plot_dataset_type * d)
{
    if (!d)
	return -1;

    return d->color;
}

plot_style_type plot_datset_get_style(plot_dataset_type * d)
{
    if (!d)
	return -1;

    return d->style;
}

double *plot_datset_get_vector_x(plot_dataset_type * d)
{
    if (!d)
	return NULL;

    return d->xvalue;
}

double *plot_datset_get_vector_y(plot_dataset_type * d)
{
    if (!d)
	return NULL;

    return d->yvalue;
}

void plot_datset_set_style(plot_dataset_type * d, plot_style_type s)
{
    if (!d)
	return;

    d->style = s;
}

/**
 * @return Returns a new plot_dataset_type pointer.
 * @brief Create a new plot_dataset_type
 *
 * Create a new dataset - allocates the memory.
 */
plot_dataset_type *plot_dataset_alloc()
{
    plot_dataset_type *d;

    d = malloc(sizeof *d);
    if (!d)
	return NULL;

    return d;
}

/**
 * @brief Free your dataset item
 * @param d your current dataset
 *
 * Use this function to free your allocated memory from plot_dataset_alloc().
 */
void plot_dataset_free(plot_dataset_type * d)
{
    util_safe_free(d->xvalue);
    util_safe_free(d->yvalue);
    util_safe_free(d);
}

/**
 * @brief Set the collected data to the dataset.
 * @param d your current dataset
 * @param x vector containing x-data
 * @param y vector containing y-data
 * @param len length of vectors
 * @param c color for the graph
 * @param s style for the graph
 *
 * After collecting your x-y data you have to let the dataset item know about
 * it. At the same time you define some detail about how the graph should look.
 */
void
plot_dataset_set_data(plot_dataset_type * d, double *x, double *y,
		      int len, plot_color_type c, plot_style_type s)
{
    if (!d) {
	fprintf(stderr,
		"Error: you need to allocate the new dataset first\n");
	return;
    }

    d->xvalue = x;
    d->yvalue = y;
    d->length = len;
    d->color = c;
    d->style = s;
    d->step = 0;
    d->finished = false;
}

void plot_dataset_join(plot_type * item, plot_dataset_type * d, int from,
		       int to)
{
    int i, k, k2;
    double *x = d->xvalue;
    double *y = d->yvalue;

    plsstrm(plot_get_stream(item));
    printf("item: %p, dataset: %p, FROM %d\t TO: %d\n", item, d, from, to);

    for (i = 0; i < (to - from); i++) {
	k = from + i;
        k2 = k + 1;

	printf("plotting from %d -> %d: %f, %f to %f, %f\n",
	       k, k2, x[k], y[k], x[k2], y[k2]);
	plplot_canvas_join(plot_get_canvas(item),
			   x[k], y[k], x[k2], y[k2]);
        plplot_canvas_adv(plot_get_canvas(item), 0);
    }

}

void plot_dataset(plot_type * item, plot_dataset_type * d)
{
    plsstrm(plot_get_stream(item));

    if (plot_get_window_type(item) == CANVAS) {
	plplot_canvas_col0(plot_get_canvas(item),
			   (PLINT) plot_datset_get_color(d));

    } else {
	plcol0((PLINT) plot_datset_get_color(d));
    }

    switch (plot_datset_get_style(d)) {
    case HISTOGRAM:
	break;
    case LINE:
	plwid(1.8);
	if (plot_get_window_type(item) == CANVAS) {
	    plplot_canvas_line(plot_get_canvas(item),
			       plot_datset_get_length(d),
			       plot_datset_get_vector_x(d),
			       plot_datset_get_vector_y(d));

	} else {
	    plline(plot_datset_get_length(d),
		   plot_datset_get_vector_x(d),
		   plot_datset_get_vector_y(d));
	}
	break;
    case POINT:
	if (plot_get_window_type(item) == CANVAS) {
	    plplot_canvas_ssym(plot_get_canvas(item), 0, 0.6);
	    plplot_canvas_poin(plot_get_canvas(item),
			       plot_datset_get_length(d),
			       plot_datset_get_vector_x(d),
			       plot_datset_get_vector_y(d), 17);

	} else {
	    plssym(0, 0.6);
	    plpoin(plot_datset_get_length(d),
		   plot_datset_get_vector_x(d),
		   plot_datset_get_vector_y(d), 17);
	}
	break;
    default:
	fprintf(stderr, "Error: no plot style is defined!\n");
	break;
    }

    if (plot_get_window_type(item) == CANVAS)
        plplot_canvas_adv(plot_get_canvas(item), 0);

}


/**
 * @brief Add a dataset to the plot 
 * @param item your current plot
 * @param d your current dataset
 *
 * When the data is in place in the dataset you can add it to the plot item,
 * this way it will be included when you do the plot.
 */
int plot_dataset_add(plot_type * item, plot_dataset_type * d)
{
    if (!d || !item) {
	fprintf(stderr,
		"Error: you need to allocate a new dataset or plot-item.\n");
	return false;
    }

    if (!d->xvalue || !d->yvalue || !d->length) {
	fprintf(stderr, "Error: you need to set the data first\n");
	return false;
    }

    printf("ID[%d] %s: Adding dataset %p to list with length %d\n",
	   plot_get_stream(item), __func__, d, d->length);
    list_append_ref(plot_get_datasets(item), d);

    return true;
}
