#!/bin/env python3
#
#--------------------------------------------------------
# LVS Manager GUI.
#
# This is a Python tkinter script that handles the
# process of running LVS and interpreting results.
#
#--------------------------------------------------------
# Written by Tim Edwards
# efabless, inc.
# Version 1.  November 30, 2016
# Version 2.  March 6, 2017.  Reads JSON format output
# Version 3.  April 25, 2018. Handles layout vs. verilog
#--------------------------------------------------------

import io
import os
import re
import sys
import json
import shutil
import signal
import socket
import select
import datetime
import contextlib
import subprocess

import tkinter
from tkinter import ttk
from tkinter import filedialog

import tksimpledialog
import tooltip
from consoletext import ConsoleText
from helpwindow import HelpWindow
from treeviewsplit import TreeViewSplit

# User preferences file (if it exists)
prefsfile = '~/.profile/prefs.json'

netgen_script_dir = '/usr/local/lib/netgen/python'

#------------------------------------------------------
# Simple dialog for confirming quit
#------------------------------------------------------

class ConfirmDialog(tksimpledialog.Dialog):
    def body(self, master, warning, seed):
        if warning:
            ttk.Label(master, text=warning, wraplength=500).grid(row = 0, columnspan = 2, sticky = 'wns')
        return self

    def apply(self):
        return 'okay'

#------------------------------------------------------
# Main class for this application
#------------------------------------------------------

class LVSManager(ttk.Frame):
    """LVS Manager GUI."""

    def __init__(self, parent, *args, **kwargs):
        ttk.Frame.__init__(self, parent, *args, **kwargs)
        self.root = parent
        self.init_gui()
        parent.protocol("WM_DELETE_WINDOW", self.on_quit)

    def on_quit(self):
        """Exits program."""
        if self.msock:
            self.msock.close()
        quit()

    def init_gui(self):
        """Builds GUI."""
        global prefsfile

        message = []
        fontsize = 11

        # Read user preferences file, get default font size from it.
        prefspath = os.path.expanduser(prefsfile)
        if os.path.exists(prefspath):
            with open(prefspath, 'r') as f:
                self.prefs = json.load(f)
            if 'fontsize' in self.prefs:
                fontsize = self.prefs['fontsize']
        else:
            self.prefs = {}

        s = ttk.Style()

        available_themes = s.theme_names()
        s.theme_use(available_themes[0])

        s.configure('bg.TFrame', background='gray40')
        s.configure('italic.TLabel', font=('Helvetica', fontsize, 'italic'))
        s.configure('title.TLabel', font=('Helvetica', fontsize, 'bold italic'),
                        foreground = 'brown', anchor = 'center')
        s.configure('normal.TLabel', font=('Helvetica', fontsize))
        s.configure('red.TLabel', font=('Helvetica', fontsize), foreground = 'red')
        s.configure('green.TLabel', font=('Helvetica', fontsize), foreground = 'green3')
        s.configure('blue.TLabel', font=('Helvetica', fontsize), foreground = 'blue')
        s.configure('normal.TButton', font=('Helvetica', fontsize),
                        border = 3, relief = 'raised')
        s.configure('red.TButton', font=('Helvetica', fontsize), foreground = 'red',
                        border = 3, relief = 'raised')
        s.configure('green.TButton', font=('Helvetica', fontsize), foreground = 'green3',
                        border = 3, relief = 'raised')
        s.configure('blue.TButton', font=('Helvetica', fontsize), foreground = 'blue',
                        border = 3, relief = 'raised')
        s.configure('redtitle.TButton', font=('Helvetica', fontsize, 'bold italic'),
                        foreground = 'red', border = 3, relief = 'raised')
        s.configure('bluetitle.TButton', font=('Helvetica', fontsize, 'bold italic'),
                        foreground = 'blue', border = 3, relief = 'raised')

        # These values to be overridden from arguments
        self.rootpath = None
        self.project = None
        self.logfile = None
        self.msock = None
        self.help = None

        # Create the help window
        if os.path.exists(netgen_script_dir + '/netgen_help.txt'):
            self.help = HelpWindow(self, fontsize = fontsize)
            with io.StringIO() as buf, contextlib.redirect_stdout(buf):
                self.help.add_pages_from_file('lvs_help.txt')
                message = buf.getvalue()

            # Set the help display to the first page
            self.help.page(0)

        # Variables used by option menus and other stuff
        self.project = "(no selection)"
        self.layout = "(default)"
        self.schematic = "(default)"
        self.tech = "(none)"
        self.lvs_setup = ''
        self.lvsdata = {}

        # Root window title
        self.root.title('LVS Manager')
        self.root.option_add('*tearOff', 'FALSE')
        self.pack(side = 'top', fill = 'both', expand = 'true')

        pane = tkinter.PanedWindow(self, orient = 'vertical', sashrelief='groove', sashwidth=6)
        pane.pack(side = 'top', fill = 'both', expand = 'true')
        self.toppane = ttk.Frame(pane)
        self.botpane = ttk.Frame(pane)

        # Get username
        if 'username' in self.prefs:
            username = self.prefs['username']
        else:
            username = os.environ['USER']

        # Label with the user
        self.toppane.title_frame = ttk.Frame(self.toppane)
        self.toppane.title_frame.pack(side = 'top', fill = 'x')

        self.toppane.title_frame.title = ttk.Label(self.toppane.title_frame, text='User:', style = 'red.TLabel')
        self.toppane.title_frame.user = ttk.Label(self.toppane.title_frame, text=username, style = 'blue.TLabel')

        self.toppane.title_frame.title.grid(column=0, row=0, ipadx = 5)
        self.toppane.title_frame.user.grid(column=1, row=0, ipadx = 5)

        self.toppane.title2_frame = ttk.Frame(self.toppane)
        self.toppane.title2_frame.pack(side = 'top', fill = 'x')
        self.toppane.title2_frame.project_label = ttk.Label(self.toppane.title2_frame, text="Project:",
                style = 'title.TLabel')
        self.toppane.title2_frame.project_label.grid(column=0, row=0, ipadx = 5)

        # New project select button
        self.toppane.title2_frame.project_select = ttk.Button(self.toppane.title2_frame,
                text=self.project, style='normal.TButton', command=self.choose_project)
        self.toppane.title2_frame.project_select.grid(column=1, row=0, ipadx = 5)

        tooltip.ToolTip(self.toppane.title2_frame.project_select,
                        text = "Select new project")

        # Show path to project
        self.toppane.title2_frame.path_label = ttk.Label(self.toppane.title2_frame, text=self.project,
                style = 'normal.TLabel')
        self.toppane.title2_frame.path_label.grid(column=2, row=0, ipadx = 5, padx = 10)

        # Show top-level layout cellname with select button.  Initial cell name is the top-level cell.
        self.toppane.title2_frame.tech_label = ttk.Label(self.toppane.title2_frame, text="Technology setup:",
                style = 'title.TLabel')

        self.toppane.title2_frame.tech_label.grid(column=3, row=0, ipadx = 5)
        self.toppane.title2_frame.tech_select = ttk.Button(self.toppane.title2_frame,
                text=self.tech, style='normal.TButton', command=self.choose_tech)
        self.toppane.title2_frame.tech_select.grid(column=4, row=0, ipadx = 3, padx = 3)

        self.toppane.title2_frame.layout_label = ttk.Label(self.toppane.title2_frame, text="Layout:",
                style = 'title.TLabel')
        self.toppane.title2_frame.layout_label.grid(column=0, row=1, ipadx = 5)
        self.toppane.title2_frame.layout_select = ttk.Button(self.toppane.title2_frame,
                text=self.layout, style='normal.TButton', command=self.choose_layout)
        self.toppane.title2_frame.layout_select.grid(column=1, row=1, ipadx = 3, padx = 3)

        # Show top-level schematic cellname with select button.  Initial cell name is the top-level cell.
        self.toppane.title2_frame.schem_label = ttk.Label(self.toppane.title2_frame, text="Schematic:",
                style = 'title.TLabel')
        self.toppane.title2_frame.schem_label.grid(column=3, row=1, ipadx = 5)
        self.toppane.title2_frame.schem_select = ttk.Button(self.toppane.title2_frame,
                text=self.schematic, style='normal.TButton', command=self.choose_netlist)
        self.toppane.title2_frame.schem_select.grid(column=4, row=1, ipadx = 3, padx = 3)

        tooltip.ToolTip(self.toppane.title2_frame.project_select,
                        text = "Select new project")
        tooltip.ToolTip(self.toppane.title2_frame.layout_select,
                        text = "Select a layout subcirucit to compare")
        tooltip.ToolTip(self.toppane.title2_frame.schem_select,
                        text = "Select a schematic subcirucit to compare")

        #---------------------------------------------
        ttk.Separator(self.toppane, orient='horizontal').pack(side = 'top', fill = 'x')
        #---------------------------------------------

        # Create listbox of Circuit1 vs. Circuit2 results
        height = 10
        self.toppane.lvsreport = TreeViewSplit(self.toppane, fontsize = fontsize)
        self.toppane.lvsreport.populate("Layout:", [], "Schematic:", [],
			[["Run", True, self.run_lvs],
			# ["Find", True, self.findrecord]
			], height = height)
        self.toppane.lvsreport.set_title("Line")
        self.toppane.lvsreport.pack(side = 'top', fill = 'both', expand = 'true')

        tooltip.ToolTip(self.toppane.lvsreport.get_button(0), text="Run LVS")

        #---------------------------------------------
        # ttk.Separator(self, orient='horizontal').grid(column=0, row=3, sticky='ew')
        #---------------------------------------------

        # Add a text window below the project name to capture output.  Redirect
        # print statements to it.

        self.botpane.console = ttk.Frame(self.botpane)
        self.botpane.console.pack(side = 'top', fill = 'both', expand = 'true')

        self.text_box = ConsoleText(self.botpane.console, wrap='word', height = 4)
        self.text_box.pack(side='left', fill='both', expand='true')
        console_scrollbar = ttk.Scrollbar(self.botpane.console)
        console_scrollbar.pack(side='right', fill='y')
        # attach console to scrollbar
        self.text_box.config(yscrollcommand = console_scrollbar.set)
        console_scrollbar.config(command = self.text_box.yview)

        # Add button bar at the bottom of the window
        self.botpane.bbar = ttk.Frame(self.botpane)
        self.botpane.bbar.pack(side = 'top', fill = 'x')

        # Define the "quit" button and action
        self.botpane.bbar.quit_button = ttk.Button(self.botpane.bbar, text='Quit', command=self.on_quit,
                style = 'normal.TButton')
        self.botpane.bbar.quit_button.grid(column=0, row=0, padx = 5)

        # Define help button
        if self.help:
            self.botpane.bbar.help_button = ttk.Button(self.botpane.bbar, text='Help',
		command=self.help.open, style = 'normal.TButton')
            self.botpane.bbar.help_button.grid(column = 2, row = 0, padx = 5)
            tooltip.ToolTip(self.botpane.bbar.help_button, text = "Show help window")

        # Add the panes once the internal geometry is known.
        pane.add(self.toppane)
        pane.add(self.botpane)
        pane.paneconfig(self.toppane, stretch='first')

        # Redirect stdout and stderr to the console as the last thing to do. . .
        # Otherwise errors in the GUI get sucked into the void.

        self.stdout = sys.stdout
        self.stderr = sys.stderr
        sys.stdout = ConsoleText.StdoutRedirector(self.text_box)
        sys.stderr = ConsoleText.StderrRedirector(self.text_box)

        if message:
            print(message)

    def logprint(self, message, doflush=False):
        if self.logfile:
            self.logfile.buffer.write(message.encode('utf-8'))
            self.logfile.buffer.write('\n'.encode('utf-8'))
            if doflush:
                self.logfile.flush()

    def printout(self, output):
        # Generate output
        if not output:
            return

        outlines = output.splitlines()
        for line in outlines:
            try:
                print(line)
            except TypeError:
                line = line.decode('utf-8')
                pritn(line)

    def printwarn(self, output):
        # Check output for warning or error
        if not output:
            return 0

        warnrex = re.compile('.*warning', re.IGNORECASE)
        errrex = re.compile('.*error', re.IGNORECASE)

        errors = 0
        outlines = output.splitlines()
        for line in outlines:
            try:
                wmatch = warnrex.match(line)
            except TypeError:
                line = line.decode('utf-8')
                wmatch = warnrex.match(line)
            ematch = errrex.match(line)
            if ematch:
                errors += 1
            if ematch or wmatch:
                print(line)
        return errors

    def choose_tech(self):
        try:
            project_path = self.rootpath
            initdirname = self.rootpath + '/tech',
        except:
            print('Must choose a project first.')
            return
        techname = filedialog.askopenfilename(multiple=False,
			initialdir = initdirname,
			filetypes = (("Tcl script", "*.tcl"),("All Files","*.*")),
			title = "Choose a netgen technology setup script.")
        if techname != '':
            print("Selected technology setup script " + techname)
            techbase = os.path.split(techname)[1]
            self.tech = os.path.splitext(techbase)[0]
            self.lvs_setup = techname
            self.toppane.title2_frame.tech_select.config(text = self.tech)

    def choose_layout(self):
        try:
            project_path = self.rootpath
            initdirname = self.rootpath + '/layout',
        except:
            print('Must choose a project first.')
            return
        cellname = filedialog.askopenfilename(multiple=False,
			initialdir = initdirname,
			filetypes = (("Magic layout", "*.mag"),("All Files","*.*")),
			title = "Choose a layout cell to compare.")
        if cellname != '':
            print("Selected compare cell " + cellname)
            self.layout = cellname
            cellbase = os.path.split(cellname)[1]
            layoutname = os.path.splitext(cellbase)[0]
            self.toppane.title2_frame.layout_select.config(text = layoutname)
            fileext = os.path.splitext(cellbase)[1]
            if fileext == '.mag':
                self.toppane.title2_frame.layout_label.config(text = 'Layout:')
            else:
                self.toppane.title2_frame.layout_label.config(text = 'Layout netlist:')

    def choose_netlist(self):
        try:
            project_path = self.rootpath
            initdirname = self.rootpath + '/netlist/' + self.project + '.spice'
        except:
            print('Must choose a project first.')
            return
        cellname = filedialog.askopenfilename(multiple=False,
			initialdir = initdirname,
			filetypes = (("Spice netlist", "*.spice"),("Verilog netlist", "*.v"),("All Files","*.*")),
			title = "Choose a netlist to compare.")
        if cellname != '':
            print("Selected compare cell " + cellname)
            self.schematic = cellname
            cellbase = os.path.split(cellname)[1]
            schematic_name = os.path.splitext(cellbase)[0]
            self.toppane.title2_frame.schem_select.config(text = schematic_name)
            fileext = os.path.splitext(cellbase)[1]
            if fileext == '.v':
                self.toppane.title2_frame.schem_label.config(text = 'Verilog netlist:')
            elif fileext == '.sp' or fileext == '.spice' or fileext == '.spi' or fileext == '.spc' or fileext == '.ckt':
                self.toppane.title2_frame.schem_label.config(text = 'SPICE netlist:')
            elif fileext == '.cdl':
                self.toppane.title2_frame.schem_label.config(text = 'CDL netlist:')
            else:
                self.toppane.title2_frame.schem_label.config(text = 'Unknown netlist:')

    def choose_project(self):
        project = filedialog.askdirectory(initialdir = os.getcwd(),
			title = "Find a project.")
        if project != '':
            print("Selected project " + str(project))
            result = self.set_project(project)

    def set_project(self, rootpath, project_name=None):

        # Check if rootpath is valid.  For LVS, there should be subdirectories
        # "layout/" and "netlist/" or "verilog/".

        haslay = os.path.isdir(rootpath + '/layout')
        hasvlog = os.path.isdir(rootpath + '/verilog')
        hasnet = os.path.isdir(rootpath + '/netlist')
        if not haslay or not (hasvlog or hasnet):
            if not haslay:
                print("Project path has no layout (/layout) subdirectory.")
            if not (hasvlog or hasnet):
                print("Project path has no verilog (/verilog), or netlist (/netlist) subdirectory.")
            # Continue anyway;  assume that netlists will be selected manually

        if self.logfile:
            self.logfile.close()
            self.logfile = None

        if not project_name:
            project = os.path.split(rootpath)[1]
        else:
            project = project_name

        if self.project != project:

            self.rootpath = rootpath
            self.project = project

            # Clear out old project data
            self.toppane.lvsreport.repopulate([], [])

            # Close any open logfile.
            if self.logfile:
                self.logfile.close()
                self.logfile = None

            # Put new log file called 'lvs.log' in the mag/ subdirectory
            if os.path.exists(rootpath + '/layout'):
                self.logfile = open(rootpath + '/layout/lvs.log', 'w')
            else:
                self.logfile = open(rootpath + '/lvs.log', 'w')
            # Print some initial information to the logfile.
            self.logprint('Starting new log file ' + datetime.datetime.now().strftime('%c'),
				doflush=True)

            # Update project button
            self.toppane.title2_frame.project_select.config(text = self.project)
            self.toppane.title2_frame.path_label.config(text = self.rootpath)
            # Cell name is the same as project name initially
            self.layout = self.project
            self.schematic = self.project
            layname = os.path.splitext(os.path.split(self.layout)[1])[0]
            self.toppane.title2_frame.layout_select.config(text = layname)
            schemname = os.path.splitext(os.path.split(self.schematic)[1])[0]
            self.toppane.title2_frame.schem_select.config(text = schemname)

            # Update schematic button
            if os.path.splitext(self.schematic)[1] == '.v':
                self.toppane.title2_frame.schem_label.config(text = 'Verilog netlist:')
            else:
                self.toppane.title2_frame.schem_label.config(text = 'Schematic netlist:')

            # Update layout button
            if os.path.splitext(self.layout)[1] == '.mag':
                self.toppane.title2_frame.schem_label.config(text = 'Layout:')
            else:
                self.toppane.title2_frame.schem_label.config(text = 'Layout netlist:')

            # If there is a comparison file that post-dates both netlists, load it.
            self.check_lvs()
        return True

    def check_layout_out_of_date(self, spipath, layoutpath):
        # Check if a netlist (spipath) is out-of-date relative to the layouts
        # (layoutpath).  Need to read the netlist and check all of the subcells.
        need_capture = False
        if not os.path.isfile(spipath):
            return True
        if os.path.isfile(layoutpath):
            spi_statbuf = os.stat(spipath)
            lay_statbuf = os.stat(layoutpath)
            if spi_statbuf.st_mtime < lay_statbuf.st_mtime:
                # netlist exists but is out-of-date
                need_capture = True
            else:
                # only found that the top-level-layout is older than the
                # netlist.  Now need to read the netlist, find all subcircuits,
                # and check those dates, too.
                layoutdir = os.path.split(layoutpath)[0]
                subrex = re.compile('^[^\*]*[ \t]*.subckt[ \t]+([^ \t]+).*$', re.IGNORECASE)
                with open(spipath, 'r') as ifile:
                    duttext = ifile.read()
 
                dutlines = duttext.replace('\n+', ' ').splitlines()
                for line in dutlines:
                    lmatch = subrex.match(line)
                    if lmatch:
                        subname = lmatch.group(1)
                        sublayout = layoutdir + '/' + subname + '.mag'
                        # subcircuits that cannot be found in the current directory are
                        # assumed to be library components and therefore never out-of-date.
                        if os.path.exists(sublayout):
                            sub_statbuf = os.stat(sublayout)
                            if spi_statbuf.st_mtime < lay_statbuf.st_mtime:
                                # netlist exists but is out-of-date
                                need_capture = True
                                break
        return need_capture

    def check_schematic_out_of_date(self, spipath, schempath):
        # Check if a netlist (spipath) is out-of-date relative to the schematics
        # (schempath).  Need to read the netlist and check all of the subcells.
        need_capture = False
        if not os.path.isfile(spipath):
            return True
        if os.path.isfile(schempath):
            spi_statbuf = os.stat(spipath)
            sch_statbuf = os.stat(schempath)
            if spi_statbuf.st_mtime < sch_statbuf.st_mtime:
                # netlist exists but is out-of-date
                need_capture = True
            else:
                # only found that the top-level-schematic is older than the
                # netlist.  Now need to read the netlist, find all subcircuits,
                # and check those dates, too.
                schemdir = os.path.split(schempath)[0]
                subrex = re.compile('^[^\*]*[ \t]*.subckt[ \t]+([^ \t]+).*$', re.IGNORECASE)
                with open(spipath, 'r') as ifile:
                    duttext = ifile.read()
 
                dutlines = duttext.replace('\n+', ' ').splitlines()
                for line in dutlines:
                    lmatch = subrex.match(line)
                    if lmatch:
                        subname = lmatch.group(1)
                        # NOTE: Electric uses library:cell internally to track libraries,
                        # and maps the ":" to "__" in the netlist.  Not entirely certain that
                        # the double-underscore uniquely identifies the library:cell. . .
                        librex = re.compile('(.*)__(.*)', re.IGNORECASE)
                        lmatch = librex.match(subname)
                        if lmatch:
                            elecpath = os.path.split(os.path.split(schempath)[0])[0]
                            libname = lmatch.group(1)
                            subschem = elecpath + '/' + libname + '.delib/' + lmatch.group(2) + '.sch'
                        else:
                            libname = {}
                            subschem = schemdir + '/' + subname + '.sch'
                        # subcircuits that cannot be found in the current directory are
                        # assumed to be library components and therefore never out-of-date.
                        if os.path.exists(subschem):
                            sub_statbuf = os.stat(subschem)
                            if spi_statbuf.st_mtime < sub_statbuf.st_mtime:
                                # netlist exists but is out-of-date
                                need_capture = True
                                break
                        # mapping of characters to what's allowed in SPICE makes finding
                        # the associated schematic file a bit difficult.  Requires wild-card
                        # searching.
                        elif libname:
                            restr = lmatch.group(2) + '.sch'
                            restr = restr.replace('.', '\.')
                            restr = restr.replace('_', '.')
                            schrex = re.compile(restr, re.IGNORECASE)
                            libpath = elecpath + '/' + libname + '.delib'
                            if os.path.exists(libpath):
                                liblist = os.listdir(libpath)
                                for file in liblist:
                                    lmatch = schrex.match(file)
                                    if lmatch:
                                        subschem = libpath + '/' + file
                                        sub_statbuf = os.stat(subschem)
                                        if spi_statbuf.st_mtime < sch_statbuf.st_mtime:
                                            # netlist exists but is out-of-date
                                            need_capture = True
                                        break
        return need_capture

    def check_lvs(self):
        # If both netlists exist, and comp.json is more recent than both, then
        # load LVS results from comp.json
        project_path = self.rootpath
        project_name = self.project
        layout_path = project_path + '/layout/' + project_name + '.spice'
        net_path = project_path + '/netlist/' + project_name + '.spice'
        comp_path = project_path + '/layout/comp.json'

        if os.path.exists(layout_path) and os.path.exists(net_path) and os.path.exists(comp_path):
            magtime = os.stat(layout_path).st_mtime
            schemtime = os.stat(net_path).st_mtime
            comptime = os.stat(comp_path).st_mtime
            if comptime > magtime and comptime > schemtime:
                print("Loading LVS results from file.")
                self.generate(comp_path)

    def generate_layout_netlist(self, layout_path, layout_src, project_path):
        # Does layout netlist exist and is it current?
        if self.check_layout_out_of_date(layout_path, layout_src):
            print('Generating layout netlist.')
            self.update_idletasks()
            mproc = subprocess.Popen(['magic', '-dnull', '-noconsole',
			self.layout], stdin = subprocess.PIPE, stdout = subprocess.PIPE,
			stderr = subprocess.PIPE, cwd = project_path + '/layout',
			universal_newlines = True)
            mproc.stdin.write("select top cell\n")
            mproc.stdin.write("expand\n")
            mproc.stdin.write("extract all\n")
            mproc.stdin.write("ext2spice hierarchy on\n")
            mproc.stdin.write("ext2spice format ngspice\n")
            mproc.stdin.write("ext2spice scale off\n")
            mproc.stdin.write("ext2spice renumber off\n")
            mproc.stdin.write("ext2spice subcircuit top auto\n")
            mproc.stdin.write("ext2spice cthresh infinite\n")
            mproc.stdin.write("ext2spice rthresh infinite\n")
            mproc.stdin.write("ext2spice blackbox on\n")
            mproc.stdin.write("ext2spice -o " + self.layout + ".spice\n")
            mproc.stdin.write("quit -noprompt\n")
            magicout = mproc.communicate()[0]
            self.printwarn(magicout)
            if mproc.returncode != 0:
                print('Failure to generate new layout netlist.')
                return False

            # Move .spice netlist to project_dir/netlist/lvs/
            shutil.move(project_path + '/layout/' + self.layout + '.spice', layout_path)
            # Remove extraction files
            for file in os.listdir(project_path + '/layout'):
                if os.path.splitext(file)[1] == '.ext':
                    os.remove(project_path + '/layout/' + file)
        else:
            print('Layout netlist is up-to-date, not regenerating.')
        return True

    def run_lvs(self, value):
        # "value" is ignored (?)

        # Check if netlists exist and are current;  otherwise create them.
        # Then run LVS.

        project_path = self.rootpath
        project_name = self.project

        # Diagnostic:
        print('project_name is ' + project_name)
        print('project_path is ' + project_path)
        print('self.layout is ' + self.layout)
        print('self.schematic is ' + self.schematic)
        print('self.lvs_setup is ' + self.lvs_setup)

        has_vlog = False
        vlog_path = project_path + '/verilog/' + project_name + '.v'

        if os.path.isfile(self.layout):
            layout_path = self.layout
            layout_src = None
        else:
            layout_path = project_path + '/netlist/lvs/' + self.layout + '.spice'
            layout_src = project_path + '/layout/' + self.layout + '.mag'

        comp_dir = os.path.split(layout_path)[0]
        comp_path = comp_dir + '/comp.json'

        if os.path.isfile(self.schematic):
            net_path = self.schematic
        else:
            net_path = project_path + '/netlist/schem/' + self.schematic + '.spice'

        # Does the setup file exist (this is optional)?
        if self.lvs_setup == '' and not os.path.isfile('setup.tcl'):
            print('No technology setup file selected.')
        elif not os.path.isfile(self.lvs_setup):
            print("Can't find technology setup file " + self.lvs_setup)

        # Does schematic netlist exist?
        if not os.path.isfile(vlog_path) and not os.path.isfile(net_path):
            print('Error:  No schematic netlist or verilog netlist.')
            return

        # Does LVS netlist subdirectory exist?
        if os.path.exists(project_path + '/netlist'):
            if not os.path.exists(project_path + '/netlist/lvs'):
                os.makedirs(project_path + '/netlist/lvs')

        # Does layout netlist exist and is it current?
        if layout_src:
            if not self.generate_layout_netlist(layout_path, layout_src, project_path):
                return False

        # Final checks
        if not os.path.isfile(layout_path):
            print('Error:  No netlist generated from magic.')
            return

        else:
            # Read in netlist and convert commas from [X,Y] arrays to vertical bars
            # as something that can be converted back as necessary.  ngspice treats
            # commas as special characters for some reason.
            with open(layout_path) as ifile:
                spitext = ifile.read()

        # Check the netlist to see if the cell to match is a subcircuit.  If
        # not, then assume it is the top level.

        layoutcell = os.path.splitext(os.path.split(self.layout)[1])[0]
        is_subckt = False
        subname = None
        subrex = re.compile('^[^\*]*[ \t]*.subckt[ \t]+([^ \t]+).*$', re.IGNORECASE)
        dutlines = spitext.replace('\n+', ' ').splitlines()
        for line in dutlines:
            lmatch = subrex.match(line)
            if lmatch:
                subname = lmatch.group(1)
                if subname == layoutcell:
                    is_subckt = True
                    break

        if is_subckt:
            layout_arg = self.layout + ' ' + layoutcell
            layout_text = '"' + layout_arg + '"'
        elif subname:
            layout_arg = self.layout + ' ' + subname
            layout_text = '"' + layout_arg + '"'
        else:
            layout_arg = layout_path
            layout_text = layout_arg

        if has_vlog:
            schem_arg = vlog_path + ' ' + self.schematic
        else:
            # Final checks
            if not os.path.isfile(net_path):
                print('Error:  No netlist from schematic.')
                return

            with open(net_path) as ifile:
                spitext = ifile.read()

            # Check the netlist to see if the cell to match is a subcircuit.  If
            # not, then assume it is the top level.

            schemcell = os.path.splitext(os.path.split(self.schematic)[1])[0]
            subname = None
            is_subckt = False
            subrex = re.compile('^[^\*]*[ \t]*.subckt[ \t]+([^ \t]+).*$', re.IGNORECASE)
            dutlines = spitext.replace('\n+', ' ').splitlines()
            for line in dutlines:
                lmatch = subrex.match(line)
                if lmatch:
                    subname = lmatch.group(1)
                    if subname == schemcell:
                        is_subckt = True
                        break

            if is_subckt:
                schem_arg = self.schematic + ' ' + schemcell
                schem_text = '"' + schem_arg + '"'
            elif subname:
                schem_arg = self.schematic + ' ' + subname
                schem_text = '"' + schem_arg + '"'
            else:
                schem_arg = net_path
                schem_text = schem_arg

        # Remove any previous comparison output file
        comp_out_path = os.path.splitext(comp_path)[0] + '.out'
        if os.path.exists(comp_out_path):
            os.remove(comp_out_path)

        # Run netgen as subprocess
        print('Running: netgen -batch lvs ' + layout_text + 
		' ' + schem_text + ' ' + self.lvs_setup + ' ' + comp_out_path +
		' -json -blackbox')
        # Note: Because arguments to subprocess are list items, the {filename cell}
        # pair does *not* have to be quoted or braced.  Doing so causes a parse
        # error.
        self.lvsproc = subprocess.Popen(['netgen', '-batch', 'lvs',
		layout_arg, schem_arg,
		self.lvs_setup, comp_out_path, '-json', '-blackbox'],
		cwd=comp_dir,
		stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        # This is largely unnecessary as netgen usually runs to completion very quickly.
        self.watchclock(comp_path)

    def watchclock(self, filename):
        if self.lvsproc == None:
            return

        lvs_status = self.lvsproc.poll()
        sresult = select.select([self.lvsproc.stdout, self.lvsproc.stderr], [], [], 0)[0]
        if self.lvsproc.stdout in sresult:
            outstring = self.lvsproc.stdout.readline().decode().strip()
            self.logprint(outstring, doflush=True)
            print(outstring)
        elif self.lvsproc.stderr in sresult:
            errstring = self.lvsproc.stderr.readline().decode().strip()
            self.logprint(errstring, doflush = True)
            print(errstring, file=sys.stderr)

        if lvs_status != None:
            print("netgen LVS exited with status " + str(lvs_status))
            self.lvsproc = None
            if lvs_status != 0:
                print('Errors encountered in LVS.')
                self.logprint('Errors in LVS, lvs status = ' + str(lvs_status), doflush=True)
            # Done;  now read comp.json and fill the treeview listbox.
            self.generate(filename)
        else:
            self.after(500, lambda: self.watchclock(filename))

    # Generate display from "comp.out" file (json file now preferred)

    def generate_orig(self, lvspath):
        lefttext = []
        righttext = []
        print("Reading LVS output file " + lvspath)
        if os.path.exists(lvspath):
            with open(lvspath, 'r') as ifile:
                lvslines = ifile.read().splitlines()
            for line in lvslines:
                if '|' in line:
                    # parts = line.split('|')
                    # lefttext.append(parts[0])
                    # righttext.append(parts[1])
                    lefttext.append(line[0:42].strip())
                    righttext.append(line[44:].strip())
                else:
                    lefttext.append(line)
                    righttext.append('')
            # Populate treeview with text
            self.toppane.lvsreport.repopulate(lefttext, righttext)
 
        else:
            print("Error:  No output file generated from LVS.")

    # Generate output from LVS report JSON file comp.json

    def generate(self, lvspath):
        lefttext = []
        righttext = []
        print("Reading LVS output file " + lvspath)
        if os.path.exists(lvspath):
            with open(lvspath, 'r') as ifile:
                self.lvsdata = json.load(ifile)

            # Populate treeview with text
            self.toppane.lvsreport.json_repopulate(self.lvsdata)
 
        else:
            print("Error:  No output file generated from LVS.")

    def findrecord(self, value):
        print("Unimplemented function")

    def findrecord_test(self, value):
        # Check if socket is defined;  if not, attempt to open one
        if not self.msock:
            try:
                self.msock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            except:
                 print("No response from layout tool.")

            if self.msock:
                self.msock.connect(("0.0.0.0", 12946))
                self.msock.setblocking(False)
        if self.msock:
            # Pull name of net or device from 'value'
            # This is a test:
            self.msock.sendall(b'box 0 0 100 100\r\n')

if __name__ == '__main__':
    options = []
    arguments = []
    for item in sys.argv[1:]:
        if item.find('-', 0) == 0:
            options.append(item)
        else:
            arguments.append(item)

    root = tkinter.Tk()
    app = LVSManager(root)
    if arguments:
        if len(arguments) >= 2:
            app.set_project(arguments[0], project_name=arguments[1])
        else:
            app.set_project(arguments[0])

    root.mainloop()
