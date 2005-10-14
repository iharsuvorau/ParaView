/*=========================================================================

  Program:   ParaView
  Module:    vtkPVFileEntry.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkPVFileEntry.h"

#include "vtkArrayMap.txx"
#include "vtkKWEntry.h"
#include "vtkKWEntry.h"
#include "vtkKWFrame.h"
#include "vtkKWLabel.h"
#include "vtkKWLoadSaveDialog.h"
#include "vtkKWMenu.h"
#include "vtkKWPushButton.h"
#include "vtkKWScaleWithEntry.h"
#include "vtkObjectFactory.h"
#include "vtkPVApplication.h"
#include "vtkPVListBoxToListBoxSelectionEditor.h"
#include "vtkPVProcessModule.h"
#include "vtkPVReaderModule.h"
#include "vtkPVWindow.h"
#include "vtkPVXMLElement.h"
#include "vtkStringList.h"
#include "vtkCommand.h"
#include "vtkKWPopupButton.h"
#include "vtkKWEvent.h"
#include "vtkSMStringListDomain.h"
#include "vtkSMStringVectorProperty.h"
#include "vtkPVTraceHelper.h"

#include <vtksys/SystemTools.hxx>

//===========================================================================
//***************************************************************************
class vtkPVFileEntryObserver: public vtkCommand
{
public:
  static vtkPVFileEntryObserver *New() 
    {return new vtkPVFileEntryObserver;};

  vtkPVFileEntryObserver()
    {
    this->FileEntry= 0;
    }

  virtual void Execute(vtkObject* wdg, unsigned long event,  
    void* calldata)
    {
    if ( this->FileEntry)
      {
      this->FileEntry->ExecuteEvent(wdg, event, calldata);
      }
    }

  vtkPVFileEntry* FileEntry;
};

//***************************************************************************
//===========================================================================

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPVFileEntry);
vtkCxxRevisionMacro(vtkPVFileEntry, "1.119");

//----------------------------------------------------------------------------
vtkPVFileEntry::vtkPVFileEntry()
{
  this->Observer = vtkPVFileEntryObserver::New();
  this->Observer->FileEntry = this;
  this->LabelWidget = vtkKWLabel::New();
  this->Entry = vtkKWEntry::New();
  this->BrowseButton = vtkKWPushButton::New();
  this->Extension = NULL;
  this->InSetValue = 0;

  this->TimestepFrame = vtkKWFrame::New();
  this->Timestep = vtkKWScaleWithEntry::New();
  this->TimeStep = 0;
  
  this->Path = 0;

  this->FileListPopup = vtkKWPopupButton::New();

  this->FileListSelect = vtkPVListBoxToListBoxSelectionEditor::New();
  this->ListObserverTag = 0;
  this->IgnoreFileListEvents = 0;

  this->Initialized = 0;
}

//----------------------------------------------------------------------------
vtkPVFileEntry::~vtkPVFileEntry()
{
  if ( this->ListObserverTag )
    {
    this->FileListSelect->RemoveObserver(this->ListObserverTag);
    }
  this->Observer->FileEntry = 0;
  this->Observer->Delete();
  this->Observer = 0;
  this->BrowseButton->Delete();
  this->BrowseButton = NULL;
  this->Entry->Delete();
  this->Entry = NULL;
  this->LabelWidget->Delete();
  this->LabelWidget = NULL;
  this->SetExtension(NULL);

  this->Timestep->Delete();
  this->TimestepFrame->Delete();
  this->FileListPopup->Delete();
  this->FileListPopup = 0;
  this->FileListSelect->Delete();
  this->FileListSelect = 0;
  
  this->SetPath(0);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::SetLabel(const char* label)
{
  // For getting the widget in a script.
  this->LabelWidget->SetText(label);

  if (label && label[0] &&
      (this->GetTraceHelper()->GetObjectNameState() == 
       vtkPVTraceHelper::ObjectNameStateUninitialized ||
       this->GetTraceHelper()->GetObjectNameState() == 
       vtkPVTraceHelper::ObjectNameStateDefault) )
    {
    this->GetTraceHelper()->SetObjectName(label);
    this->GetTraceHelper()->SetObjectNameState(
      vtkPVTraceHelper::ObjectNameStateSelfInitialized);
    }
}

//----------------------------------------------------------------------------
const char* vtkPVFileEntry::GetLabel()
{
  return this->LabelWidget->GetText();
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::SetBalloonHelpString(const char *str)
{
  this->Superclass::SetBalloonHelpString(str);

  if (this->LabelWidget)
    {
    this->LabelWidget->SetBalloonHelpString(str);
    }

  if (this->Entry)
    {
    this->Entry->SetBalloonHelpString(str);
    }

  if (this->BrowseButton)
    {
    this->BrowseButton->SetBalloonHelpString(str);
    }
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::Create(vtkKWApplication *app)
{
  // Check if already created

  if (this->IsCreated())
    {
    vtkErrorMacro(<< this->GetClassName() << " already created");
    return;
    }

  // Call the superclass to create the whole widget

  this->Superclass::Create(app);

  vtkKWFrame* frame = vtkKWFrame::New();
  frame->SetParent(this);
  frame->Create(app);

  this->LabelWidget->SetParent(frame);
  this->Entry->SetParent(frame);
  this->BrowseButton->SetParent(frame);
  
  // Now a label
  this->LabelWidget->Create(app);
  this->LabelWidget->SetWidth(18);
  this->LabelWidget->SetJustificationToRight();
  this->Script("pack %s -side left", this->LabelWidget->GetWidgetName());
  
  // Now the entry
  this->Entry->Create(app);
  this->Entry->SetWidth(8);
  this->Script("bind %s <KeyPress> {%s ModifiedCallback}",
               this->Entry->GetWidgetName(), this->GetTclName());
  this->Entry->SetCommand(this, "EntryChangedCallback");
  // Change the order of the bindings so that the
  // modified command gets called after the entry changes.
  this->Script("bindtags %s [concat Entry [lreplace [bindtags %s] 1 1]]", 
               this->Entry->GetWidgetName(), this->Entry->GetWidgetName());
  this->Script("pack %s -side left -fill x -expand t",
               this->Entry->GetWidgetName());
  
  // Now the push button
  this->BrowseButton->Create(app);
  this->BrowseButton->SetText("Browse");
  this->BrowseButton->SetCommand(this, "BrowseCallback");

  this->Script("pack %s -side left", this->BrowseButton->GetWidgetName());
  this->Script("pack %s -fill both -expand 1", frame->GetWidgetName());

  this->TimestepFrame->SetParent(this);
  this->TimestepFrame->Create(app);
  this->Timestep->SetParent(this->TimestepFrame);
  this->Timestep->Create(app);
  this->Script("pack %s -expand 1 -fill both", this->Timestep->GetWidgetName());
  this->Script("pack %s -side bottom -expand 1 -fill x", this->TimestepFrame->GetWidgetName());
  this->Script("pack forget %s", this->TimestepFrame->GetWidgetName());
  this->Timestep->SetLabelText("Timestep");
  this->Timestep->RangeVisibilityOn();
  this->Timestep->SetEndCommand(this, "TimestepChangedCallback");
  this->Timestep->SetEntryCommand(this, "TimestepChangedCallback");

  this->FileListPopup->SetParent(frame);
  this->FileListPopup->Create(app);
  this->FileListPopup->SetText("Timesteps");
  this->FileListPopup->SetPopupTitle("Select Files For Time Series");
  this->FileListPopup->SetCommand(this, "UpdateAvailableFiles");

  this->FileListSelect->SetParent(this->FileListPopup->GetPopupFrame());
  this->FileListSelect->Create(app);
  this->Script("pack %s -fill both -expand 1", this->FileListSelect->GetWidgetName());
  this->Script("pack %s -fill x", this->FileListPopup->GetWidgetName());

  this->ListObserverTag = this->FileListSelect->AddObserver(
    vtkCommand::WidgetModifiedEvent, 
    this->Observer);
  frame->Delete();

  this->FileListSelect->SetEllipsisCommand(this, "UpdateAvailableFiles 1");
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::EntryChangedCallback()
{
  const char* val = this->Entry->GetValue();
  this->SetValue(val);
}

//-----------------------------------------------------------------------------
void vtkPVFileEntry::SetTimeStep(int ts)
{
  vtkSMProperty *prop = this->GetSMProperty();
  vtkSMStringListDomain *dom = 0;

  if (prop)
    {
    dom = vtkSMStringListDomain::SafeDownCast(prop->GetDomain("files"));
    }
  
  if (!prop || !dom)
    {
    vtkErrorMacro("Property or domain (files) could not be found.");
    return;
    }
  
  int numStrings = dom->GetNumberOfStrings();
  if ( ts >= numStrings || ts < 0 )
    {
    return;
    }

  if (this->Initialized)
    {
    const char* fname = dom->GetString(ts);
    if ( fname )
      {
      if ( fname[0] == '/' || 
           (fname[1] == ':' && (fname[2] == '/' || fname[2] == '\\')) ||
           (fname[0] == '\\' && fname[1] == '\\') ||
           !this->Path || !*this->Path)
        {
        this->SetValue(fname);
        }
      else
        {
        ostrstream str;
        str << this->Path << "/" << fname << ends;
        this->SetValue(str.str());
        str.rdbuf()->freeze(0);
        }
      }
    }

  this->Timestep->SetValue(ts);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::TimestepChangedCallback()
{
  int ts = static_cast<int>(this->Timestep->GetValue());
  this->SetTimeStep(ts);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::BrowseCallback()
{
  ostrstream str;
  vtkKWLoadSaveDialog* loadDialog = this->GetPVApplication()->NewLoadSaveDialog();
  const char* fname = this->Entry->GetValue();

  vtkPVApplication* pvApp = this->GetPVApplication();
  vtkPVWindow* win = 0;
  if (pvApp)
    {
    win = pvApp->GetMainWindow();
    }
  if (fname && fname[0])
    {
    vtksys_stl::string path = vtksys::SystemTools::GetFilenamePath(fname);
    if (path.size())
      {
      loadDialog->SetLastPath(path.c_str());
      }
    }
  else
    {
      this->GetApplication()->RetrieveDialogLastPathRegistryValue(loadDialog, "OpenPath");
    }
  loadDialog->Create(this->GetPVApplication());
  if (win) 
    { 
    loadDialog->SetParent(this); 
    }
  loadDialog->SetTitle(this->GetLabel()?this->GetLabel():"Select File");
  if(this->Extension)
    {
    loadDialog->SetDefaultExtension(this->Extension);
    str << "{{} {." << this->Extension << "}} ";
    }
  str << "{{All files} {*}}" << ends;  
  loadDialog->SetFileTypes(str.str());
  str.rdbuf()->freeze(0);  
  if(loadDialog->Invoke())
    {
    this->Script("%s SetValue {%s}", this->GetTclName(),
                 loadDialog->GetFileName());
    }
  loadDialog->Delete();
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::SetValue(const char* fileName)
{
  if ( this->InSetValue )
    {
    return;
    }

  const char *old;
  
  if (fileName == NULL || fileName[0] == 0)
    {
    return;
    }

  old = this->Entry->GetValue();
  if (strcmp(old, fileName) == 0)
    {
    return;
    }

  vtkSMProperty *prop = this->GetSMProperty();
  vtkSMStringListDomain *dom = 0;
  
  if (prop)
    {
    dom = vtkSMStringListDomain::SafeDownCast(prop->GetDomain("files"));
    }
  
  if (!prop || !dom)
    {
    vtkErrorMacro("Property or domain (files) could not be found.");
    return;
    }
  
  this->InSetValue = 1;

  this->Entry->SetValue(fileName); 

  int already_set = 0;
  int cc;

  const char* prefix = 0;
  char* format = 0;
  already_set = dom->GetNumberOfStrings();

  vtksys_stl::string path = vtksys::SystemTools::GetFilenamePath(fileName);
  if ( !this->Path || strcmp(this->Path, path.c_str()) != 0 )
    {
    already_set = 0;
    this->SetPath(path.c_str());
    }

  if ( already_set )
    {
    // Already set, so just return
    this->InSetValue = 0;
    this->ModifiedCallback();
    return;
    }

  this->IgnoreFileListEvents = 1;

  this->FileListSelect->RemoveItemsFromFinalList();

  // Have to regenerate prefix, pattern...

  vtkPVProcessModule* pm = this->GetPVApplication()->GetProcessModule();
  vtkStringList* files = vtkStringList::New();

  char* number = new char [ strlen(fileName) + 1];

  vtksys_stl::string file = vtksys::SystemTools::GetFilenameName(fileName);
  vtksys_stl::string ext = vtksys::SystemTools::GetFilenameExtension(fileName);
  if (ext.size() >= 1)
    {
    ext.erase(0,1); // Get rid of the "." GetFilenameExtension returns.
    }
  int in_ext = 1;
  int in_num = 0;

  int fnameLength = 0;

  if (strcmp(ext.c_str(), "h5") == 0)
    {
    file[file.size()-1] = 'f';
    }

  int ncnt = 0;
  for ( cc = (int)(file.size())-1; cc >= 0; cc -- )
    {
    if ( file[cc] >= '0' && file[cc] <= '9' )
      {
      in_num = 1;
      number[ncnt] = file[cc];
      ncnt ++;
      }
    else if ( in_ext && file[cc] == '.' )
      {
      in_ext = 0;
      in_num = 1;
      ncnt = 0;
      }
    else if ( in_num )
      {
      break;
      }
    file.erase(cc, file.size() - cc);
    }

  if (path.size())
    {
    prefix = file.c_str();
    number[ncnt] = 0;
    for ( cc = 0; cc < ncnt/2; cc ++ )
      {
      char tmp = number[cc];
      number[cc] = number[ncnt-cc-1];
      number[ncnt-cc-1] = tmp;
      }
    char firstformat[100];
    char secondformat[100];
    sprintf(firstformat, "%%s/%%s%%0%dd.%%s", ncnt);
    sprintf(secondformat, "%%s/%%s%%d.%%s");
    pm->GetDirectoryListing(path.c_str(), 0, files, 0);

    this->FileListSelect->SetSourceList(files);

    int med = atoi(number);
    fnameLength = (int)(strlen(fileName)) * 2;
    char* rfname = new char[ fnameLength ];
    int min;
    int max;
    
    // Find upper limit.
    int increment = 2;
    for ( max = med; increment!=0; )
      {
      sprintf(rfname, firstformat, path.c_str(), file.c_str(), 
        (max + increment), ext.c_str());
      if ( files->GetIndex(rfname+path.size()+1) >= 0 )
        {
        // Found!
        max += increment;
        increment <<= 1;
        }
      else
        {
        increment >>= 1;
        }
      }
    
    // Find lower limit.
    increment = 2;
    for ( min = med; increment!=0; )
      {
      sprintf(rfname, firstformat, path.c_str(), file.c_str(), 
        (min - increment), ext.c_str());
      if ( files->GetIndex(rfname+path.size()+1) >= 0 )
        {
        // Found!
        min -= increment;
        increment <<= 1;
        }
      else
        {
        increment >>= 1;
        }
      }

    int smin;
    int smax;
     // Find upper limit.
    increment = 2;
    for ( smax = med; increment!=0; )
      {
      sprintf(rfname, secondformat, path.c_str(), file.c_str(), 
        (smax + increment), ext.c_str());
      if ( files->GetIndex(rfname+path.size()+1) >= 0 )
        {
        // Found!
        smax += increment;
        increment <<= 1;
        }
      else
        {
        increment >>= 1;
        }
      }
    
    // Find lower limit.
    increment = 2;
    for ( smin = med; increment!=0; )
      {
      sprintf(rfname, secondformat, path.c_str(), file.c_str(), 
        (smin - increment), ext.c_str());
      if ( files->GetIndex(rfname+path.size()+1) >= 0 )
        {
        // Found!
        smin -= increment;
        increment <<= 1;
        }
      else
        {
        increment >>= 1;
        }
      }
    delete [] rfname;

    // If second range is bigger than first range, use second format
    if ( (smax - smin) >= (max - min) )
      {
      format = secondformat;
      min = smin;
      max = smax;
      }
    else
      {
      format = firstformat;
      }
    if ( max - min)
      {
      char* name = new char [ fnameLength ];
      vtkStringList* finallist = vtkStringList::New();
      for ( cc = min; cc <= max; cc ++ )
        {
        sprintf(name, format, path.c_str(), prefix, cc, ext.c_str());
        vtksys_stl::string shname = vtksys::SystemTools::GetFilenameName(name);
        if ( files->GetIndex(shname.c_str()) >= 0 )
          {
          finallist->AddString(shname.c_str());
          }
        }
      this->FileListSelect->SetFinalList(finallist, 1);
      finallist->Delete();
      delete [] name;
      }
    }

  if ( !this->FileListSelect->GetNumberOfElementsOnFinalList() )
    {
    file = vtksys::SystemTools::GetFilenameName(fileName);
    this->FileListSelect->AddFinalElement(file.c_str(), 1);
    }
  
  if ( !this->Initialized )
    {
    dom->RemoveAllStrings();
    int kk;
    for ( kk = 0; kk < this->FileListSelect->GetNumberOfElementsOnFinalList();
          kk ++ )
      {
      ostrstream str;
      if (this->Path && this->Path[0])
        {
        str << this->Path << "/";
        }
      str << this->FileListSelect->GetElementFromFinalList(kk) << ends;
      dom->AddString(str.str());
      str.rdbuf()->freeze(0);
      }
    vtksys_stl::string cfile = vtksys::SystemTools::GetFilenameName(fileName);
    ostrstream fullPath;
    fullPath << this->Path << "/" << cfile.c_str() << ends;
    unsigned int i;
    for ( i = 0; i < dom->GetNumberOfStrings(); i ++ )
      {
      if ( strcmp(fullPath.str(), dom->GetString(i)) == 0 )
        {
        this->SetTimeStep(i);
        this->TimeStep = i;
        break;
        }
      }
    fullPath.rdbuf()->freeze(0);
    this->Initialized = 1;
    }

  files->Delete();
  delete [] number;

  this->UpdateTimeStep();

  this->IgnoreFileListEvents = 0;
  this->InSetValue = 0;
  this->ModifiedCallback();
}

//---------------------------------------------------------------------------
void vtkPVFileEntry::Trace(ofstream *file)
{
  if ( ! this->GetTraceHelper()->Initialize(file))
    {
    return;
    }

  // I assume the quotes are for eveluating an output tcl variable.
  *file << "$kw(" << this->GetTclName() << ") SetValue \""
        << this->GetValue() << "\"" << endl;
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::Accept()
{
  const char* fname = this->Entry->GetValue();
  
  this->TimeStep = static_cast<int>(this->Timestep->GetValue());

  vtkSMStringVectorProperty *svp = vtkSMStringVectorProperty::SafeDownCast(
    this->GetSMProperty());
  
  if (svp)
    {
    svp->SetElement(0, fname);
    }
  
  vtkPVReaderModule* rm = vtkPVReaderModule::SafeDownCast(this->PVSource);
  if (rm && fname && fname[0])
    {
    const char* desc = rm->RemovePath(fname);
    if (desc)
      {
      rm->SetLabelOnce(desc);
      }
    }

  this->UpdateTimesteps();

  vtkSMStringListDomain *sld = vtkSMStringListDomain::SafeDownCast(
    svp->GetDomain("files"));

  if (sld)
    {
    sld->RemoveAllStrings();
    int cc;
    for ( cc = 0; cc < this->FileListSelect->GetNumberOfElementsOnFinalList();
          cc ++ )
      {
      ostrstream str;
      if (this->Path && this->Path[0])
        {
        str << this->Path << "/";
        }
      str << this->FileListSelect->GetElementFromFinalList(cc) << ends;
      sld->AddString(str.str());
      str.rdbuf()->freeze(0);
      }
    }
  else
    {
    vtkErrorMacro("Required domain (files) could not be found.");
    }

  this->UpdateAvailableFiles();

  this->Superclass::Accept();
}


//----------------------------------------------------------------------------
void vtkPVFileEntry::UpdateTimesteps()
{
  const char* fullfilename = this->GetValue();
 
  // Check to see if the new filename value specified, is among the timesteps already
  // selected. If so, in that case, the user is attempting to merely change the 
  // timestep using the file name entry. Otherwise, the user is choosing a new dataset
  // may be, so we just get rid of old timesteps.
  int max_elems = this->FileListSelect->GetNumberOfElementsOnFinalList();
  int cc;
  int is_present = 0;

  vtksys_stl::string filename = 
    vtksys::SystemTools::GetFilenameName(fullfilename);
  
  for (cc = 0; cc < max_elems; cc++)
    {
    if (strcmp(filename.c_str(), 
               this->FileListSelect->GetElementFromFinalList(cc))==0)
      {
      is_present = 1;
      break;
      }
    }
  if (is_present)
    {
    return;
    }
  // clear the file entries.
  this->IgnoreFileListEvents = 1;
  this->FileListSelect->RemoveItemsFromFinalList();
  this->FileListSelect->AddFinalElement(filename.c_str());
  this->IgnoreFileListEvents = 0;
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::Initialize()
{
  vtkSMStringVectorProperty *svp = vtkSMStringVectorProperty::SafeDownCast(
    this->GetSMProperty());

  if (svp)
    {
    this->SetValue(svp->GetElement(0));
    this->SetTimeStep(this->TimeStep);

    vtkSMStringListDomain *sld = vtkSMStringListDomain::SafeDownCast(
      svp->GetDomain("files"));
    if (sld)
      {
      this->IgnoreFileListEvents = 1;
      vtkStringList* finallist = vtkStringList::New();
      unsigned int cc;
      for ( cc = 0; cc < sld->GetNumberOfStrings(); cc ++ )
        {
        vtksys_stl::string filename = 
          vtksys::SystemTools::GetFilenameName(sld->GetString(cc));
        finallist->AddString(filename.c_str());
        }
      this->FileListSelect->SetFinalList(finallist, 1);
      finallist->Delete();
      }
    else
      {
      vtkErrorMacro("Required domain (files) could not be found.");
      }
    }

  const char* fileName = this->Entry->GetValue();
  if ( fileName && fileName[0] )
    {
    vtksys_stl::string file = vtksys::SystemTools::GetFilenameName(fileName);
    this->FileListSelect->AddFinalElement(file.c_str(), 1);
    }

  this->IgnoreFileListEvents = 0;

  this->UpdateAvailableFiles();
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::ResetInternal()
{

  this->Initialize();
  this->ModifiedFlag = 0;
}

//----------------------------------------------------------------------------
vtkPVFileEntry* vtkPVFileEntry::ClonePrototype(vtkPVSource* pvSource,
                                 vtkArrayMap<vtkPVWidget*, vtkPVWidget*>* map)
{
  vtkPVWidget* clone = this->ClonePrototypeInternal(pvSource, map);
  return vtkPVFileEntry::SafeDownCast(clone);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::CopyProperties(vtkPVWidget* clone, vtkPVSource* pvSource,
                              vtkArrayMap<vtkPVWidget*, vtkPVWidget*>* map)
{
  this->Superclass::CopyProperties(clone, pvSource, map);
  vtkPVFileEntry* pvfe = vtkPVFileEntry::SafeDownCast(clone);
  if (pvfe)
    {
    pvfe->LabelWidget->SetText(this->LabelWidget->GetText());
    pvfe->SetExtension(this->GetExtension());
    }
  else 
    {
    vtkErrorMacro("Internal error. Could not downcast clone to PVFileEntry.");
    }
}

//----------------------------------------------------------------------------
int vtkPVFileEntry::ReadXMLAttributes(vtkPVXMLElement* element,
                                      vtkPVXMLPackageParser* parser)
{
  if(!this->Superclass::ReadXMLAttributes(element, parser)) { return 0; }
  
  // Setup the Label.
  const char* label = element->GetAttribute("label");
  if(label)
    {
    this->SetLabel(label);
    }
  else
    {
    this->SetLabel("File Name");
    }
  
  // Setup the Extension.
  const char* extension = element->GetAttribute("extension");
  if(!extension)
    {
    vtkErrorMacro("No extension attribute.");
    return 0;
    }
  this->SetExtension(extension);
  
  return 1;
}

//----------------------------------------------------------------------------
const char* vtkPVFileEntry::GetValue() 
{
  return this->Entry->GetValue();
}

//-----------------------------------------------------------------------------
int vtkPVFileEntry::GetNumberOfFiles()
{
  vtkSMProperty *prop = this->GetSMProperty();
  vtkSMStringListDomain *dom = 0;
  
  if (prop)
    {
    dom = vtkSMStringListDomain::SafeDownCast(prop->GetDomain("files"));
    }
  
  if ( !dom )
    {
    vtkErrorMacro("Required domain (files) could not be found.");
    return 0;
    }
  return dom->GetNumberOfStrings();
}

//-----------------------------------------------------------------------------
void vtkPVFileEntry::SaveInBatchScript(ofstream* file)
{
  vtkSMProperty *prop = this->GetSMProperty();
  vtkSMStringListDomain *dom = 0;

  if (prop)
    {
    dom = vtkSMStringListDomain::SafeDownCast(prop->GetDomain("files"));
    }
  
  if (!dom)
    {
    vtkErrorMacro("Required domain (files) could not be found.");
    return;
    }

  vtkClientServerID sourceID = this->PVSource->GetVTKSourceID(0);

  if (sourceID.ID == 0 || !this->SMPropertyName)
    {
    vtkErrorMacro("Sanity check failed. " << this->GetClassName());
    return;
    }

  if ( dom->GetNumberOfStrings() > 1 )
    {
    *file << "set " << "pvTemp" << sourceID << "_files {";
    unsigned int cc;
    for ( cc = 0; cc <  dom->GetNumberOfStrings(); cc ++ )
      {
      *file << "\"" << dom->GetString(cc) << "\" ";
      }
    *file << "}" << endl;

    *file << "  [$pvTemp" << sourceID
          <<  " GetProperty " << this->SMPropertyName << "] SetElement 0 "
          << " [ lindex $" << "pvTemp" << sourceID
          << "_files " << this->TimeStep << "]" << endl;

    }
  else
    {
    *file << "  [$pvTemp" << sourceID
          <<  " GetProperty " << this->SMPropertyName << "] SetElement 0 {"
          << this->Entry->GetValue() << "}" << endl;
    }
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::ExecuteEvent(vtkObject*, unsigned long event, void*)
{
  if ( event == vtkCommand::WidgetModifiedEvent && !this->IgnoreFileListEvents )
    {
    this->UpdateTimeStep();
    this->ModifiedCallback();
    }
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::UpdateTimeStep()
{
  const char* fileName = this->Entry->GetValue();
  if ( !fileName || !fileName[0] )
    {
    return;
    }

  // If the reader module has another time widget, do not show
  // the time slider.
  vtkPVReaderModule* rm = vtkPVReaderModule::SafeDownCast(this->PVSource);
  if (rm)
    {
    vtkPVWidget* tsw = rm->GetTimeStepWidget();
    if (tsw && tsw != this)
      {
      return;
      }
    }

  this->IgnoreFileListEvents = 1;
  vtksys_stl::string file = vtksys::SystemTools::GetFilenameName(fileName);
  this->FileListSelect->AddFinalElement(file.c_str(), 1);
  int ts = this->FileListSelect->GetElementIndexFromFinalList(file.c_str());
  if ( ts < 0 )
    {
    cerr << "This should not have happended" << endl;
    cerr << "Cannot find \"" << file.c_str() << "\" on the list" << endl;
    int cc;
    for ( cc = 0; cc < this->FileListSelect->GetNumberOfElementsOnFinalList(); cc ++ )
      {
      cerr << "Element: " << this->FileListSelect->GetElementFromFinalList(cc) << endl;
      }
    vtkPVApplication::Abort();
    }
  this->Timestep->SetValue(ts);
  if ( this->FileListSelect->GetNumberOfElementsOnFinalList() > 1 )
    {
    this->Script("pack %s -side bottom -expand 1 -fill x", 
      this->TimestepFrame->GetWidgetName());
    this->Timestep->SetRange(0, 
      this->FileListSelect->GetNumberOfElementsOnFinalList()-1);
    }
  else
    {
    this->Script("pack forget %s", 
      this->TimestepFrame->GetWidgetName());
    }
  this->IgnoreFileListEvents = 0;

}

//----------------------------------------------------------------------------
void vtkPVFileEntry::UpdateAvailableFiles( int force )
{
  if ( !this->Path )
    {
    return;
    }
  vtkPVProcessModule* pm = this->GetPVApplication()->GetProcessModule();
  vtkStringList* files = vtkStringList::New();
  pm->GetDirectoryListing(this->Path, 0, files, 0);

  if (force)
    {
    this->IgnoreFileListEvents = 1;
    this->FileListSelect->RemoveItemsFromSourceList();
    this->FileListSelect->SetSourceList(files);
    this->IgnoreFileListEvents = 0;
    }
  files->Delete();
  this->UpdateTimeStep();
}

//-----------------------------------------------------------------------------
void vtkPVFileEntry::UpdateEnableState()
{
  this->Superclass::UpdateEnableState();

  this->PropagateEnableState(this->LabelWidget);
  this->PropagateEnableState(this->BrowseButton);
  this->PropagateEnableState(this->Entry);
  this->PropagateEnableState(this->TimestepFrame);
  this->PropagateEnableState(this->Timestep);
  this->PropagateEnableState(this->FileListSelect);
  this->PropagateEnableState(this->FileListPopup);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << "Extension: " << (this->Extension?this->Extension:"none") << endl;
}
