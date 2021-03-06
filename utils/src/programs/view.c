//
//  Visopsys
//  Copyright (C) 1998-2015 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  view.c
//

// This command will display supported file types in the appropriate way.

/* This is the text that appears when a user requests help about this program
<help>

 -- view --

View a file.

Usage:
  view [file]

(Only available in graphics mode)

This command will launch a window in which the requested file is displayed.
If no file name is supplied on the command line (or for example if the
program is launched by clicking on its icon), the user will be prompted for
the file to display.

The currently-supported file formats are:

- Images (currently only uncompressed, 8-bit and 24-bit bitmap images)
- Text files

</help>
*/

#include <libgen.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/font.h>
#include <sys/paths.h>
#include <sys/vsh.h>
#include <sys/window.h>

#define _(string) gettext(string)
#define gettext_noop(string) (string)

#define WINDOW_TITLE  _("View \"%s\"")

// Right-click menu for images
#define IMAGEMENU_ZOOMIN		0
#define IMAGEMENU_ZOOMOUT		1
#define IMAGEMENU_ACTUAL		2
static windowMenuContents imageMenuContents = {
	3,
	{
		{ gettext_noop("Zoom in"), NULL },
		{ gettext_noop("Zoom out"), NULL },
		{ gettext_noop("Actual size"), NULL },
	}
};

static int graphics = 0;
static char *fileName = NULL;
static char *shortName = NULL;
static char *windowTitle = NULL;
static image origImage;
static image showImage;
static objectKey window = NULL;
static objectKey windowImage = NULL;
static objectKey imageMenu = NULL;
static double imageScale = 0;


__attribute__((format(printf, 1, 2)))
static void error(const char *format, ...)
{
	// Generic error message code for either text or graphics modes

	va_list list;
	char output[MAXSTRINGLENGTH];

	va_start(list, format);
	vsnprintf(output, MAXSTRINGLENGTH, format, list);
	va_end(list);

	if (graphics)
		windowNewErrorDialog(NULL, _("Error"), output);
	else
		printf(_("ERROR: %s\n"), output);
}


static int countTextLines(int columns, char *data, int size)
{
	// Count the lines of text

	int lines = 1;
	int columnCount = 0;
	int count;

	for (count = 0; count < size; count ++)
	{
		if ((columnCount >= columns) || (data[count] == '\n'))
		{
			lines += 1;
			columnCount = 0;
		}

		else if (data[count] == '\0')
			break;

		else
			columnCount += 1;
	}

	return (lines);
}


static void printTextLines(char *data, int size)
{
	// Cut the text data into lines and print them individually

	int count;

	for (count = 0; count < size; count ++)
	{
		if (data[count] == '\t')
			textTab();
		else if (data[count] == '\n')
			textNewline();
		else if (data[count] == '\0')
			break;
		else
			textPutc(data[count]);
	}
}


static int resizeImage(double scale)
{
	int status = 0;
	componentParameters params;

	if (scale == imageScale)
		return (status = 0);

	status = imageCopy(&origImage, &showImage);
	if (status >= 0)
	{
		if (scale == 1.0)
		{
			// Already original size
			imageScale = scale;
		}
		else
		{
			status = imageResize(&showImage,
				(unsigned)((double) showImage.width * scale),
				(unsigned)((double) showImage.height * scale));
			if (status >= 0)
			{
				imageScale = ((double) showImage.width /
					(double) origImage.width);
			}
		}
	}

	if (windowImage)
		windowComponentDestroy(windowImage);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	windowImage = windowNewImage(window, &showImage, draw_normal, &params);
	windowContextSet(windowImage, imageMenu);

	windowLayout(window);

	sprintf(windowTitle, WINDOW_TITLE, shortName);
	sprintf((windowTitle + strlen(windowTitle)), " (%d%%)",
		(int)(imageScale * 100));
	windowSetTitle(window, windowTitle);

	return (status);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	// Handle GUI events.

	if (key == window)
	{
		if (event->type == EVENT_WINDOW_CLOSE)
			windowGuiStop();
	}

	// Check for right-click events in our image menu.

	else if (key == imageMenuContents.items[IMAGEMENU_ZOOMIN].key)
	{
		if (event->type & EVENT_SELECTION)
			resizeImage(imageScale * 1.25);
	}

	else if (key == imageMenuContents.items[IMAGEMENU_ZOOMOUT].key)
	{
		if (event->type & EVENT_SELECTION)
			resizeImage(imageScale * 0.75);
	}

	else if (key == imageMenuContents.items[IMAGEMENU_ACTUAL].key)
	{
		if (event->type & EVENT_SELECTION)
			resizeImage(1.0);
	}
}


static int viewImage(void)
{
	int status = 0;
	unsigned screenWidth = graphicGetScreenWidth();
	unsigned screenHeight = graphicGetScreenHeight();
	double xScale = 0, yScale = 0;
	objectKey bannerDialog = NULL;
	componentParameters params;
	int count;

	memset(&origImage, 0, sizeof(image));
	memset(&showImage, 0, sizeof(image));

	bannerDialog = windowNewBannerDialog(NULL, _("Loading"),
		_("Loading image..."));

	// Try to load the image file
	status = imageLoad(fileName, 0, 0, &origImage);
	if (status < 0)
	{
		if (origImage.data)
			error(_("Error loading the image \"%s\"\n"), fileName);
		else
		{
			error(_("Unable to load the image \"%s\"\n"), fileName);
			return (status);
		}
	}

	if (bannerDialog)
		windowDestroy(bannerDialog);

	// Copy the original image to one we can mess with.
	imageCopy(&origImage, &showImage);
	imageScale = 1.0;

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	windowImage = windowNewImage(window, &showImage, draw_normal, &params);

	imageMenu = windowNewMenu(window, NULL, _("Image"), &imageMenuContents,
		&params);

	for (count = 0; count < imageMenuContents.numItems; count ++)
		windowRegisterEventHandler(imageMenuContents.items[count].key,
			&eventHandler);

	windowContextSet(windowImage, imageMenu);

	// If the image is big, shrink it by default, to 2/3 of the screen.
	if (showImage.width > ((screenWidth * 2) / 3))
		xScale = ((double)((screenWidth * 2) / 3) / (double) showImage.width);
	if (showImage.height > ((screenHeight * 2) / 3))
		yScale = ((double)((screenHeight * 2) / 3) /
			(double) showImage.height);

	if (xScale || yScale)
		resizeImage(min(xScale, yScale));

	return (status = 0);
}


static int viewText(void)
{
	// Try to load the text data

	int status = 0;
	file showFile;
	char *textData = NULL;
	int rows = 25;
	int textLines = 0;
	objectKey textAreaComponent = NULL;
	componentParameters params;

	memset(&showFile, 0, sizeof(file));

	textData = loaderLoad(fileName, &showFile);
	if (!textData)
	{
		error(_("Unable to load the file \"%s\"\n"), fileName);
		return (status = ERR_IO);
	}

	textLines = countTextLines(80, textData, showFile.size);

	memset(&params, 0, sizeof(componentParameters));
	params.gridWidth = 1;
	params.gridHeight = 1;
	params.orientationX = orient_center;
	params.orientationY = orient_middle;

	status = fileFind(PATH_SYSTEM_FONTS "/xterm-normal-10.vbf", NULL);
	if (status >= 0)
		status = fontLoadSystem("xterm-normal-10.vbf", "xterm-normal-10",
			&(params.font), 1);
	if (status < 0)
	{
		// Use the system font.  It can comfortably show more rows.
		params.font = NULL;
		rows = 40;
	}

	textAreaComponent = windowNewTextArea(window, 80, rows, textLines,
		 &params);

	// Put the data into the component
	windowSetTextOutput(textAreaComponent);
	textSetCursor(0);
	textInputSetEcho(0);
	printTextLines(textData, showFile.size);

	// Scroll back to the very top
	textScroll(-(textLines / rows));

	memoryRelease(textData);
	return (status = 0);
}


int main(int argc, char *argv[])
{
	int status = 0;
	int processId = 0;
	loaderFileClass class;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("view");

	graphics = graphicsAreEnabled();

	// Only work in graphics mode
	if (!graphics)
	{
		printf(_("\nThe \"%s\" command only works in graphics mode\n"),
			argv[0]);
		return (status = ERR_NOTINITIALIZED);
	}

	processId = multitaskerGetCurrentProcessId();

	fileName = malloc(MAX_PATH_NAME_LENGTH);
	windowTitle = malloc(MAX_PATH_NAME_LENGTH + 8);
	if (!fileName || !windowTitle)
	{
		status = ERR_MEMORY;
		perror(argv[0]);
		goto deallocate;
	}

	if (argc < 2)
	{
		status = windowNewFileDialog(NULL, _("Enter filename"),
			_("Please choose the file to view:"), NULL, fileName,
			MAX_PATH_NAME_LENGTH, 1);
		if (status != 1)
		{
			if (status)
				perror(argv[0]);

			goto deallocate;
		}
	}
	else
		strncpy(fileName, argv[argc - 1], MAX_PATH_NAME_LENGTH);

	// Make sure the file exists
	if (fileFind(fileName, NULL) < 0)
	{
		error(_("The file \"%s\" was not found"), fileName);
		goto deallocate;
	}

	shortName = basename(fileName);

	// Get the classification of the file.
	if (!loaderClassifyFile(fileName, &class))
	{
		error(_("Unable to classify the file \"%s\""), fileName);
		goto deallocate;
	}

	if (!(class.class & LOADERFILECLASS_IMAGE) &&
		!(class.class & LOADERFILECLASS_TEXT))
	{
		error(_("Can't display the file type of \"%s\" (%s)"), fileName,
		class.className);
		goto deallocate;
	}

	// Create a new window
	sprintf(windowTitle, WINDOW_TITLE, shortName);
	window = windowNew(processId, windowTitle);

	if (class.class & LOADERFILECLASS_IMAGE)
		status = viewImage();
	else if (class.class & LOADERFILECLASS_TEXT)
		status = viewText();

	if (status >= 0)
	{
		// Go live.
		windowSetVisible(window, 1);

		// Register an event handler to catch window close events
		windowRegisterEventHandler(window, &eventHandler);

		// Run the GUI
		windowGuiRun();
	}

	// Destroy the window
	windowDestroy(window);

	status = 0;

deallocate:
	if (fileName)
		free(fileName);
	if (shortName)
		free(shortName);
	if (windowTitle)
		free(windowTitle);
	imageFree(&origImage);
	imageFree(&showImage);

	return (status);
}

