/*
 * PPD file routines for libppd.
 *
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 *
 * PostScript is a trademark of Adobe Systems, Inc.
 */

/*
 * Include necessary headers.
 */

//#include "string-private.h"
//#include "language-private.h"
//#include "thread-private.h"
#include "ppd.h"
#include "debug-internal.h"


/*
 * Definitions...
 */



/*
 * Local functions...
 */

static ipp_t		*create_media_col(const char *media, const char *source, const char *type, int width, int length, int bottom, int left, int right, int top);
static ipp_t		*create_media_size(int width, int length);


/*
 * 'ppdLoadAttributes()' - Load IPP attributes from a PPD file.
 */

ipp_t *					/* O - IPP attributes or `NULL`
					   on error */
ppdLoadAttributes(
    const char   *ppdfile,		/* I - PPD filename */
    cups_array_t *docformats)		/* I - document-format-supported values */
{
  int		i, j;			/* Looping vars */
  ipp_t		*attrs;			/* Attributes */
  ipp_attribute_t *attr;		/* Current attribute */
  ipp_t		*col;			/* Current collection value */
  ppd_file_t	*ppd;			/* PPD data */
  ppd_attr_t	*ppd_attr;		/* PPD attribute */
  ppd_choice_t	*ppd_choice;		/* PPD choice */
  ppd_size_t	*ppd_size;		/* Default PPD size */
  pwg_size_t	*pwg_size,		/* Current PWG size */
		*default_size = NULL;	/* Default PWG size */
  const char	*default_source = NULL,	/* Default media source */
		*default_type = NULL;	/* Default media type */
  pwg_map_t	*pwg_map;		/* Mapping from PWG to PPD keywords */
  ppd_cache_t	*pc;			/* PPD cache */
  ppd_pwg_finishings_t *finishings;	/* Current finishings value */
  const char	*template;		/* Current finishings-template value */
  int		num_margins;		/* Number of media-xxx-margin-supported values */
  int		margins[10];		/* media-xxx-margin-supported values */
  int		xres,			/* Default horizontal resolution */
		yres;			/* Default vertical resolution */
  int		num_urf;		/* Number of urf-supported values */
  const char	*urf[10];		/* urf-supported values */
  char		urf_rs[32];		/* RS value */
  static const int	orientation_requested_supported[4] =
  {					/* orientation-requested-supported values */
    IPP_ORIENT_PORTRAIT,
    IPP_ORIENT_LANDSCAPE,
    IPP_ORIENT_REVERSE_LANDSCAPE,
    IPP_ORIENT_REVERSE_PORTRAIT
  };
  static const char * const overrides_supported[] =
  {					/* overrides-supported */
    "document-numbers",
    "media",
    "media-col",
    "orientation-requested",
    "pages"
  };
  static const char * const print_color_mode_supported[] =
  {					/* print-color-mode-supported values */
    "monochrome"
  };
  static const char * const print_color_mode_supported_color[] =
  {					/* print-color-mode-supported values */
    "auto",
    "color",
    "monochrome"
  };
  static const int	print_quality_supported[] =
  {					/* print-quality-supported values */
    IPP_QUALITY_DRAFT,
    IPP_QUALITY_NORMAL,
    IPP_QUALITY_HIGH
  };
  static const char * const printer_supply[] =
  {					/* printer-supply values */
    "index=1;class=receptacleThatIsFilled;type=wasteToner;unit=percent;"
        "maxcapacity=100;level=25;colorantname=unknown;",
    "index=2;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=75;colorantname=black;"
  };
  static const char * const printer_supply_color[] =
  {					/* printer-supply values */
    "index=1;class=receptacleThatIsFilled;type=wasteInk;unit=percent;"
        "maxcapacity=100;level=25;colorantname=unknown;",
    "index=2;class=supplyThatIsConsumed;type=ink;unit=percent;"
        "maxcapacity=100;level=75;colorantname=black;",
    "index=3;class=supplyThatIsConsumed;type=ink;unit=percent;"
        "maxcapacity=100;level=50;colorantname=cyan;",
    "index=4;class=supplyThatIsConsumed;type=ink;unit=percent;"
        "maxcapacity=100;level=33;colorantname=magenta;",
    "index=5;class=supplyThatIsConsumed;type=ink;unit=percent;"
        "maxcapacity=100;level=67;colorantname=yellow;"
  };
  static const char * const printer_supply_description[] =
  {					/* printer-supply-description values */
    "Toner Waste Tank",
    "Black Toner"
  };
  static const char * const printer_supply_description_color[] =
  {					/* printer-supply-description values */
    "Ink Waste Tank",
    "Black Ink",
    "Cyan Ink",
    "Magenta Ink",
    "Yellow Ink"
  };
  static const char * const pwg_raster_document_type_supported[] =
  {
    "black_1",
    "sgray_8"
  };
  static const char * const pwg_raster_document_type_supported_color[] =
  {
    "black_1",
    "sgray_8",
    "srgb_8",
    "srgb_16"
  };
  static const char * const sides_supported[] =
  {					/* sides-supported values */
    "one-sided",
    "two-sided-long-edge",
    "two-sided-short-edge"
  };


 /*
  * Open the PPD file...
  */

  if ((ppd = ppdOpenFile(ppdfile)) == NULL)
  {
    ppd_status_t	status;		/* Load error */

    status = ppdLastError(&i);
    fprintf(stderr, "ippeveprinter: Unable to open \"%s\": %s on line %d.", ppdfile, ppdErrorString(status), i);
    return (NULL);
  }

  ppdMarkDefaults(ppd);

  pc = ppdCacheCreateWithPPD(ppd);

  if ((ppd_size = ppdPageSize(ppd, NULL)) != NULL)
  {
   /*
    * Look up default size...
    */

    for (i = 0, pwg_size = pc->sizes; i < pc->num_sizes; i ++, pwg_size ++)
    {
      if (!strcmp(pwg_size->map.ppd, ppd_size->name))
      {
        default_size = pwg_size;
        break;
      }
    }
  }

  if (!default_size)
  {
   /*
    * Default to A4 or Letter...
    */

    for (i = 0, pwg_size = pc->sizes; i < pc->num_sizes; i ++, pwg_size ++)
    {
      if (!strcmp(pwg_size->map.ppd, "Letter") || !strcmp(pwg_size->map.ppd, "A4"))
      {
        default_size = pwg_size;
        break;
      }
    }

    if (!default_size)
      default_size = pc->sizes;		/* Last resort: first size */
  }

  if ((ppd_choice = ppdFindMarkedChoice(ppd, "InputSlot")) != NULL)
    default_source = ppdCacheGetSource(pc, ppd_choice->choice);

  if ((ppd_choice = ppdFindMarkedChoice(ppd, "MediaType")) != NULL)
    default_source = ppdCacheGetType(pc, ppd_choice->choice);

  if ((ppd_attr = ppdFindAttr(ppd, "DefaultResolution", NULL)) != NULL)
  {
   /*
    * Use the PPD-defined default resolution...
    */

    if ((i = sscanf(ppd_attr->value, "%dx%d", &xres, &yres)) == 1)
      yres = xres;
    else if (i < 0)
      xres = yres = 300;
  }
  else
  {
   /*
    * Use default of 300dpi...
    */

    xres = yres = 300;
  }

  snprintf(urf_rs, sizeof(urf_rs), "RS%d", yres < xres ? yres : xres);

  num_urf = 0;
  urf[num_urf ++] = "V1.4";
  urf[num_urf ++] = "CP1";
  urf[num_urf ++] = urf_rs;
  urf[num_urf ++] = "W8";
  if (pc->sides_2sided_long)
    urf[num_urf ++] = "DM1";
  if (ppd->color_device)
    urf[num_urf ++] = "SRGB24";

 /*
  * PostScript printers accept PDF via one of the CUPS PDF to PostScript
  * filters, along with PostScript (of course) and JPEG...
  */

  cupsArrayAdd(docformats, "application/pdf");
  cupsArrayAdd(docformats, "application/postscript");
  cupsArrayAdd(docformats, "image/jpeg");

 /*
  * Create the attributes...
  */

  attrs = ippNew();

  /* color-supported */
  ippAddBoolean(attrs, IPP_TAG_PRINTER, "color-supported", (char)ppd->color_device);

  /* copies-default */
  ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "copies-default", 1);

  /* copies-supported */
  ippAddRange(attrs, IPP_TAG_PRINTER, "copies-supported", 1, 999);

  /* document-password-supported */
  ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "document-password-supported", 127);

  /* finishing-template-supported */
  attr = ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template-supported", cupsArrayCount(pc->templates) + 1, NULL, NULL);
  ippSetString(attrs, &attr, 0, "none");
  for (i = 1, template = (const char *)cupsArrayFirst(pc->templates); template; i ++, template = (const char *)cupsArrayNext(pc->templates))
    ippSetString(attrs, &attr, i, template);

  /* finishings-col-database */
  attr = ippAddCollections(attrs, IPP_TAG_PRINTER, "finishings-col-database", cupsArrayCount(pc->templates) + 1, NULL);

  col = ippNew();
  ippAddString(col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template", NULL, "none");
  ippSetCollection(attrs, &attr, 0, col);
  ippDelete(col);

  for (i = 1, template = (const char *)cupsArrayFirst(pc->templates); template; i ++, template = (const char *)cupsArrayNext(pc->templates))
  {
    col = ippNew();
    ippAddString(col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template", NULL, template);
    ippSetCollection(attrs, &attr, i, col);
    ippDelete(col);
  }

  /* finishings-col-default */
  col = ippNew();
  ippAddString(col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template", NULL, "none");
  ippAddCollection(attrs, IPP_TAG_PRINTER, "finishings-col-default", col);
  ippDelete(col);

  /* finishings-col-ready */
  attr = ippAddCollections(attrs, IPP_TAG_PRINTER, "finishings-col-ready", cupsArrayCount(pc->templates) + 1, NULL);

  col = ippNew();
  ippAddString(col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template", NULL, "none");
  ippSetCollection(attrs, &attr, 0, col);
  ippDelete(col);

  for (i = 1, template = (const char *)cupsArrayFirst(pc->templates); template; i ++, template = (const char *)cupsArrayNext(pc->templates))
  {
    col = ippNew();
    ippAddString(col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template", NULL, template);
    ippSetCollection(attrs, &attr, i, col);
    ippDelete(col);
  }

  /* finishings-col-supported */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishings-col-supported", NULL, "finishing-template");

  /* finishings-default */
  ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-default", IPP_FINISHINGS_NONE);

  /* finishings-ready */
  attr = ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-ready", cupsArrayCount(pc->finishings) + 1, NULL);
  ippSetInteger(attrs, &attr, 0, IPP_FINISHINGS_NONE);
  for (i = 1, finishings = (ppd_pwg_finishings_t *)cupsArrayFirst(pc->finishings); finishings; i ++, finishings = (ppd_pwg_finishings_t *)cupsArrayNext(pc->finishings))
    ippSetInteger(attrs, &attr, i, (int)finishings->value);

  /* finishings-supported */
  attr = ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-supported", cupsArrayCount(pc->finishings) + 1, NULL);
  ippSetInteger(attrs, &attr, 0, IPP_FINISHINGS_NONE);
  for (i = 1, finishings = (ppd_pwg_finishings_t *)cupsArrayFirst(pc->finishings); finishings; i ++, finishings = (ppd_pwg_finishings_t *)cupsArrayNext(pc->finishings))
    ippSetInteger(attrs, &attr, i, (int)finishings->value);

  /* media-bottom-margin-supported */
  for (i = 0, num_margins = 0, pwg_size = pc->sizes; i < pc->num_sizes && num_margins < (int)(sizeof(margins) / sizeof(margins[0])); i ++, pwg_size ++)
  {
    for (j = 0; j < num_margins; j ++)
    {
      if (margins[j] == pwg_size->bottom)
        break;
    }

    if (j >= num_margins)
      margins[num_margins ++] = pwg_size->bottom;
  }

  for (i = 0; i < (num_margins - 1); i ++)
  {
    for (j = i + 1; j < num_margins; j ++)
    {
      if (margins[i] > margins[j])
      {
        int mtemp = margins[i];

        margins[i] = margins[j];
        margins[j] = mtemp;
      }
    }
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-bottom-margin-supported", num_margins, margins);

  /* media-col-database */
  attr = ippAddCollections(attrs, IPP_TAG_PRINTER, "media-col-database", pc->num_sizes, NULL);
  for (i = 0, pwg_size = pc->sizes; i < pc->num_sizes; i ++, pwg_size ++)
  {
    col = create_media_col(pwg_size->map.pwg, NULL, NULL, pwg_size->width, pwg_size->length, pwg_size->bottom, pwg_size->left, pwg_size->right, pwg_size->top);
    ippSetCollection(attrs, &attr, i, col);
    ippDelete(col);
  }

  /* media-col-default */
  col = create_media_col(default_size->map.pwg, default_source, default_type, default_size->width, default_size->length, default_size->bottom, default_size->left, default_size->right, default_size->top);
  ippAddCollection(attrs, IPP_TAG_PRINTER, "media-col-default", col);
  ippDelete(col);

  /* media-col-ready */
  col = create_media_col(default_size->map.pwg, default_source, default_type, default_size->width, default_size->length, default_size->bottom, default_size->left, default_size->right, default_size->top);
  ippAddCollection(attrs, IPP_TAG_PRINTER, "media-col-ready", col);
  ippDelete(col);

  /* media-default */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-default", NULL, default_size->map.pwg);

  /* media-left-margin-supported */
  for (i = 0, num_margins = 0, pwg_size = pc->sizes; i < pc->num_sizes && num_margins < (int)(sizeof(margins) / sizeof(margins[0])); i ++, pwg_size ++)
  {
    for (j = 0; j < num_margins; j ++)
    {
      if (margins[j] == pwg_size->left)
        break;
    }

    if (j >= num_margins)
      margins[num_margins ++] = pwg_size->left;
  }

  for (i = 0; i < (num_margins - 1); i ++)
  {
    for (j = i + 1; j < num_margins; j ++)
    {
      if (margins[i] > margins[j])
      {
        int mtemp = margins[i];

        margins[i] = margins[j];
        margins[j] = mtemp;
      }
    }
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-left-margin-supported", num_margins, margins);

  /* media-ready */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-ready", NULL, default_size->map.pwg);

  /* media-right-margin-supported */
  for (i = 0, num_margins = 0, pwg_size = pc->sizes; i < pc->num_sizes && num_margins < (int)(sizeof(margins) / sizeof(margins[0])); i ++, pwg_size ++)
  {
    for (j = 0; j < num_margins; j ++)
    {
      if (margins[j] == pwg_size->right)
        break;
    }

    if (j >= num_margins)
      margins[num_margins ++] = pwg_size->right;
  }

  for (i = 0; i < (num_margins - 1); i ++)
  {
    for (j = i + 1; j < num_margins; j ++)
    {
      if (margins[i] > margins[j])
      {
        int mtemp = margins[i];

        margins[i] = margins[j];
        margins[j] = mtemp;
      }
    }
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-right-margin-supported", num_margins, margins);

  /* media-supported */
  attr = ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-supported", pc->num_sizes, NULL, NULL);
  for (i = 0, pwg_size = pc->sizes; i < pc->num_sizes; i ++, pwg_size ++)
    ippSetString(attrs, &attr, i, pwg_size->map.pwg);

  /* media-size-supported */
  attr = ippAddCollections(attrs, IPP_TAG_PRINTER, "media-size-supported", pc->num_sizes, NULL);
  for (i = 0, pwg_size = pc->sizes; i < pc->num_sizes; i ++, pwg_size ++)
  {
    col = create_media_size(pwg_size->width, pwg_size->length);
    ippSetCollection(attrs, &attr, i, col);
    ippDelete(col);
  }

  /* media-source-supported */
  if (pc->num_sources > 0)
  {
    attr = ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-source-supported", pc->num_sources, NULL,  NULL);
    for (i = 0, pwg_map = pc->sources; i < pc->num_sources; i ++, pwg_map ++)
      ippSetString(attrs, &attr, i, pwg_map->pwg);
  }
  else
  {
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-source-supported", NULL, "auto");
  }

  /* media-top-margin-supported */
  for (i = 0, num_margins = 0, pwg_size = pc->sizes; i < pc->num_sizes && num_margins < (int)(sizeof(margins) / sizeof(margins[0])); i ++, pwg_size ++)
  {
    for (j = 0; j < num_margins; j ++)
    {
      if (margins[j] == pwg_size->top)
        break;
    }

    if (j >= num_margins)
      margins[num_margins ++] = pwg_size->top;
  }

  for (i = 0; i < (num_margins - 1); i ++)
  {
    for (j = i + 1; j < num_margins; j ++)
    {
      if (margins[i] > margins[j])
      {
        int mtemp = margins[i];

        margins[i] = margins[j];
        margins[j] = mtemp;
      }
    }
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-top-margin-supported", num_margins, margins);

  /* media-type-supported */
  if (pc->num_types > 0)
  {
    attr = ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-type-supported", pc->num_types, NULL,  NULL);
    for (i = 0, pwg_map = pc->types; i < pc->num_types; i ++, pwg_map ++)
      ippSetString(attrs, &attr, i, pwg_map->pwg);
  }
  else
  {
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-type-supported", NULL, "auto");
  }

  /* orientation-requested-default */
  ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "orientation-requested-default", IPP_ORIENT_PORTRAIT);

  /* orientation-requested-supported */
  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "orientation-requested-supported", (int)(sizeof(orientation_requested_supported) / sizeof(orientation_requested_supported[0])), orientation_requested_supported);

  /* output-bin-default */
  if (pc->num_bins > 0)
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "output-bin-default", NULL, pc->bins->pwg);
  else
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "output-bin-default", NULL, "face-down");

  /* output-bin-supported */
  if (pc->num_bins > 0)
  {
    attr = ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "output-bin-supported", pc->num_bins, NULL,  NULL);
    for (i = 0, pwg_map = pc->bins; i < pc->num_bins; i ++, pwg_map ++)
      ippSetString(attrs, &attr, i, pwg_map->pwg);
  }
  else
  {
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "output-bin-supported", NULL, "face-down");
  }

  /* overrides-supported */
  ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "overrides-supported", (int)(sizeof(overrides_supported) / sizeof(overrides_supported[0])), NULL, overrides_supported);

  /* page-ranges-supported */
  ippAddBoolean(attrs, IPP_TAG_PRINTER, "page-ranges-supported", 1);

  /* pages-per-minute */
  ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "pages-per-minute", ppd->throughput);

  /* pages-per-minute-color */
  if (ppd->color_device)
    ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "pages-per-minute-color", ppd->throughput);

  /* print-color-mode-default */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-default", NULL, ppd->color_device ? "auto" : "monochrome");

  /* print-color-mode-supported */
  if (ppd->color_device)
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-supported", (int)(sizeof(print_color_mode_supported_color) / sizeof(print_color_mode_supported_color[0])), NULL, print_color_mode_supported_color);
  else
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-supported", (int)(sizeof(print_color_mode_supported) / sizeof(print_color_mode_supported[0])), NULL, print_color_mode_supported);

  /* print-content-optimize-default */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-content-optimize-default", NULL, "auto");

  /* print-content-optimize-supported */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-content-optimize-supported", NULL, "auto");

  /* print-quality-default */
  ippAddInteger(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-default", IPP_QUALITY_NORMAL);

  /* print-quality-supported */
  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-supported", (int)(sizeof(print_quality_supported) / sizeof(print_quality_supported[0])), print_quality_supported);

  /* print-rendering-intent-default */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-rendering-intent-default", NULL, "auto");

  /* print-rendering-intent-supported */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-rendering-intent-supported", NULL, "auto");

  /* printer-device-id */
  if ((ppd_attr = ppdFindAttr(ppd, "1284DeviceId", NULL)) != NULL)
  {
   /*
    * Use the device ID string from the PPD...
    */

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-device-id", NULL, ppd_attr->value);
  }
  else
  {
   /*
    * Synthesize a device ID string...
    */

    char	device_id[1024];		/* Device ID string */

    snprintf(device_id, sizeof(device_id), "MFG:%s;MDL:%s;CMD:PS;", ppd->manufacturer, ppd->modelname);

    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-device-id", NULL, device_id);
  }

  /* printer-input-tray */
  if (pc->num_sources > 0)
  {
    for (i = 0, attr = NULL; i < pc->num_sources; i ++)
    {
      char	input_tray[1024];	/* printer-input-tray value */

      if (!strcmp(pc->sources[i].pwg, "manual") || strstr(pc->sources[i].pwg, "-man") != NULL)
        snprintf(input_tray, sizeof(input_tray), "type=sheetFeedManual;mediafeed=0;mediaxfeed=0;maxcapacity=1;level=-2;status=0;name=%s", pc->sources[i].pwg);
      else
        snprintf(input_tray, sizeof(input_tray), "type=sheetFeedAutoRemovableTray;mediafeed=0;mediaxfeed=0;maxcapacity=250;level=125;status=0;name=%s", pc->sources[i].pwg);

      if (attr)
        ippSetOctetString(attrs, &attr, i, input_tray, (int)strlen(input_tray));
      else
        attr = ippAddOctetString(attrs, IPP_TAG_PRINTER, "printer-input-tray", input_tray, (int)strlen(input_tray));
    }
  }
  else
  {
    static const char *printer_input_tray = "type=sheetFeedAutoRemovableTray;mediafeed=0;mediaxfeed=0;maxcapacity=-2;level=-2;status=0;name=auto";

    ippAddOctetString(attrs, IPP_TAG_PRINTER, "printer-input-tray", printer_input_tray, (int)strlen(printer_input_tray));
  }

  /* printer-make-and-model */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", NULL, ppd->nickname);

  /* printer-resolution-default */
  ippAddResolution(attrs, IPP_TAG_PRINTER, "printer-resolution-default", IPP_RES_PER_INCH, xres, yres);

  /* printer-resolution-supported */
  ippAddResolution(attrs, IPP_TAG_PRINTER, "printer-resolution-supported", IPP_RES_PER_INCH, xres, yres);

  /* printer-supply and printer-supply-description */
  if (ppd->color_device)
  {
    attr = ippAddOctetString(attrs, IPP_TAG_PRINTER, "printer-supply", printer_supply_color[0], (int)strlen(printer_supply_color[0]));
    for (i = 1; i < (int)(sizeof(printer_supply_color) / sizeof(printer_supply_color[0])); i ++)
      ippSetOctetString(attrs, &attr, i, printer_supply_color[i], (int)strlen(printer_supply_color[i]));

    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-supply-description", (int)(sizeof(printer_supply_description_color) / sizeof(printer_supply_description_color[0])), NULL, printer_supply_description_color);
  }
  else
  {
    attr = ippAddOctetString(attrs, IPP_TAG_PRINTER, "printer-supply", printer_supply[0], (int)strlen(printer_supply[0]));
    for (i = 1; i < (int)(sizeof(printer_supply) / sizeof(printer_supply[0])); i ++)
      ippSetOctetString(attrs, &attr, i, printer_supply[i], (int)strlen(printer_supply[i]));

    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-supply-description", (int)(sizeof(printer_supply_description) / sizeof(printer_supply_description[0])), NULL, printer_supply_description);
  }

  /* pwg-raster-document-xxx-supported */
  if (cupsArrayFind(docformats, (void *)"image/pwg-raster"))
  {
    ippAddResolution(attrs, IPP_TAG_PRINTER, "pwg-raster-document-resolution-supported", IPP_RES_PER_INCH, xres, yres);

    if (pc->sides_2sided_long)
      ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "pwg-raster-document-sheet-back", NULL, "normal");

    if (ppd->color_device)
      ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "pwg-raster-document-type-supported", (int)(sizeof(pwg_raster_document_type_supported_color) / sizeof(pwg_raster_document_type_supported_color[0])), NULL, pwg_raster_document_type_supported_color);
    else
      ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "pwg-raster-document-type-supported", (int)(sizeof(pwg_raster_document_type_supported) / sizeof(pwg_raster_document_type_supported[0])), NULL, pwg_raster_document_type_supported);
  }

  /* sides-default */
  ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-default", NULL, "one-sided");

  /* sides-supported */
  if (pc->sides_2sided_long)
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-supported", (int)(sizeof(sides_supported) / sizeof(sides_supported[0])), NULL, sides_supported);
  else
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-supported", NULL, "one-sided");

  /* urf-supported */
  if (cupsArrayFind(docformats, (void *)"image/urf"))
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "urf-supported", num_urf, NULL, urf);

 /*
  * Free the PPD file and return the attributes...
  */

  ppdCacheDestroy(pc);

  ppdClose(ppd);

  return (attrs);
}

/*
 * 'create_media_col()' - Create a media-col value.
 */

static ipp_t *				/* O - media-col collection */
create_media_col(const char *media,	/* I - Media name */
		 const char *source,	/* I - Media source, if any */
		 const char *type,	/* I - Media type, if any */
		 int        width,	/* I - x-dimension in 2540ths */
		 int        length,	/* I - y-dimension in 2540ths */
		 int        bottom,	/* I - Bottom margin in 2540ths */
		 int        left,	/* I - Left margin in 2540ths */
		 int        right,	/* I - Right margin in 2540ths */
		 int        top)	/* I - Top margin in 2540ths */
{
  ipp_t		*media_col = ippNew(),	/* media-col value */
		*media_size = create_media_size(width, length);
					/* media-size value */
  char		media_key[256];		/* media-key value */
  const char	*media_key_suffix = "";	/* media-key suffix */


  if (bottom == 0 && left == 0 && right == 0 && top == 0)
    media_key_suffix = "_borderless";

  if (type && source)
    snprintf(media_key, sizeof(media_key), "%s_%s_%s%s", media, source, type, media_key_suffix);
  else if (type)
    snprintf(media_key, sizeof(media_key), "%s__%s%s", media, type, media_key_suffix);
  else if (source)
    snprintf(media_key, sizeof(media_key), "%s_%s%s", media, source, media_key_suffix);
  else
    snprintf(media_key, sizeof(media_key), "%s%s", media, media_key_suffix);

  ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-key", NULL, media_key);
  ippAddCollection(media_col, IPP_TAG_PRINTER, "media-size", media_size);
  ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-size-name", NULL, media);
  if (bottom >= 0)
    ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-bottom-margin", bottom);
  if (left >= 0)
    ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-left-margin", left);
  if (right >= 0)
    ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-right-margin", right);
  if (top >= 0)
    ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-top-margin", top);
  if (source)
    ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-source", NULL, source);
  if (type)
    ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-type", NULL, type);

  ippDelete(media_size);

  return (media_col);
}


/*
 * 'create_media_size()' - Create a media-size value.
 */

static ipp_t *				/* O - media-col collection */
create_media_size(int width,		/* I - x-dimension in 2540ths */
		  int length)		/* I - y-dimension in 2540ths */
{
  ipp_t	*media_size = ippNew();		/* media-size value */


  ippAddInteger(media_size, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "x-dimension", width);
  ippAddInteger(media_size, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "y-dimension", length);

  return (media_size);
}
