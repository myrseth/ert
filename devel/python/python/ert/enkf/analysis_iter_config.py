#  Copyright (C) 2012  Statoil ASA, Norway. 
#   
#  The file 'analysis_iter_config.py' is part of ERT - Ensemble based Reservoir Tool. 
#   
#  ERT is free software: you can redistribute it and/or modify 
#  it under the terms of the GNU General Public License as published by 
#  the Free Software Foundation, either version 3 of the License, or 
#  (at your option) any later version. 
#   
#  ERT is distributed in the hope that it will be useful, but WITHOUT ANY 
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or 
#  FITNESS FOR A PARTICULAR PURPOSE.   
#   
#  See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html> 
#  for more details. 

import  ctypes
from    ert.cwrap.cwrap       import *
from    ert.cwrap.cclass      import CClass
from    ert.util.tvector      import * 
from    enkf_enum             import *
import  libenkf

class AnalysisIterConfig(CClass):
    
def __init__(self , c_ptr , parent = None):
        if parent:
            self.init_cref( c_ptr , parent)
        else:
            self.init_cobj( c_ptr , cfunc.free )
            
    @property
    def get_num_iterations(self):
        return cfunc.get_num_iterations( self )

    def set_num_iterations(self, num_iterations):
        cfunc.set_num_iterations(self, num_iterations)

    @property
    def get_case_fmt(self):
        return cfunc.get_case_fmt( self )

    def set_case_fmt(self, case_fmt):
        cfunc.set_case_fmt(self, case_fmt)

    @property
    def get_runpath_fmt(self):
        return cfunc.get_case_fmt( self )

    def set_runpath_fmt(self, case_fmt):
        cfunc.set_case_fmt(self, case_fmt)
##################################################################

cwrapper = CWrapper( libenkf.lib )
cwrapper.registerType( "analysis_iter_config" , AnalysisIterConfig )

cfunc = CWrapperNameSpace("analysis_iter_config")


cfunc.free                   = cwrapper.prototype("void analysis_iter_config_free( analysis_iter_config )")
cfunc.set_num_iterations     = cwrapper.prototype("void analysis_iter_config_set_num_iterations(analysis_iter_config, int)")
cfunc.get_num_iterations     = cwrapper.prototype("int analysis_iter_config_get_num_iterations(analysis_iter_config)")
cfunc.set_case_fmt           = cwrapper.prototype("void analysis_iter_config_set_case_fmt( analysis_iter_config , char* )");
cfunc.get_case_fmt           = cwrapper.prototype("char* analysis_iter_config_get_case_fmt( analysis_iter_config)");
cfunc.set_runpath_fmt           = cwrapper.prototype("void analysis_iter_config_set_runpath_fmt( analysis_iter_config_type * config , char*)");
cfunc.get_runpath_fmt           = cwrapper.prototype("char* analysis_iter_config_get_runpath_fmt( analysis_iter_config)");
