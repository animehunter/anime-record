
#include <ClanLib/core.h>
#include <ClanLib/gui.h>
#include <map>
#include "MessageDialog.h"


const CL_String MessageDialog::OK = "ok";
const CL_String MessageDialog::CANCEL = "cancel";
const CL_String MessageDialog::YES = "yes";
const CL_String MessageDialog::NO = "no";
const CL_String MessageDialog::RETRY = "retry";
const CL_String MessageDialog::ABORT = "abort";

MessageDialog::MessageDialog(CL_GUIComponent *owner, const CL_String &title, const CL_String &message, DialogType dialogType) :
CL_Window(owner, getGUIDescription(title))
{
    label = new CL_Label(this);
    label->set_text(message);
    label->set_geometry(CL_RectPS(20,20,280,100));


    createDialog(dialogType);

    set_draggable(true);
    func_close().set(this, &MessageDialog::onClose);
}

MessageDialog::~MessageDialog()
{

}


CL_String MessageDialog::getResult() const
{
    return result;
}

CL_GUITopLevelDescription MessageDialog::getGUIDescription(const CL_String &title) const
{
    CL_GUITopLevelDescription desc;
    desc.set_title(title);
    desc.set_size(CL_Size(300,120), true);
    desc.show_maximize_button(false);
    desc.show_minimize_button(false);

    return desc;
}

bool MessageDialog::onClose()
{
    exit_with_code(0);
    return true;
}

CL_GUIComponent *MessageDialog::addButton( const CL_String &text )
{
    CL_PushButton *b = new CL_PushButton(this);
    b->set_text(text);
    buttons[text] = b;

    return b;
}

//////////////////////////////////////////////////////////////////////////

void MessageDialog::makeButtonGeneric( const CL_String &name, int width, int shiftFromCenter )
{
    CL_PushButton *b = new CL_PushButton(this);
    buttons[name] = b;
    b->set_text(name);
    CL_Size pos = get_geometry().get_size()/2 - CL_Size(30,20) + shiftFromCenter;
    b->set_geometry(CL_RectPS(pos.width, get_geometry().get_size().height-40, width, 20));
    b->func_clicked().set(this, &MessageDialog::onClicked, name);
    b->set_focus(true);
}
void MessageDialog::createDialog( DialogType dialogType )
{
    if(dialogType == ASK_OK)
    {
        makeButtonGeneric(OK, 30, 20);
    }
    else if(dialogType == ASK_OK_CANCEL)
    {
        makeButtonGeneric(OK, 30, -20);
        makeButtonGeneric(CANCEL, 50, 20);
    }
    else if(dialogType == ASK_YES_NO)
    {
        makeButtonGeneric(YES, 30, -20);
        makeButtonGeneric(NO, 30, 20);
    }
    else if(dialogType == ASK_YES_NO_CANCEL)
    {
        makeButtonGeneric(YES, 30, -50);
        makeButtonGeneric(NO, 30, -10);
        makeButtonGeneric(CANCEL, 50, 30);
    }
    else if(dialogType == ASK_RETRY_ABORT)
    {
        makeButtonGeneric(RETRY, 40, -25);
        makeButtonGeneric(ABORT, 40, 25);
    }
    else if(dialogType == ASK_RETRY_ABORT_CANCEL)
    {
        makeButtonGeneric(RETRY, 40, -55);
        makeButtonGeneric(ABORT, 40, -10);
        makeButtonGeneric(CANCEL, 50, 35);
    }
}

CL_GUIComponent * MessageDialog::getButton( const CL_String &text )
{
    return buttons[text];
}

void MessageDialog::onClicked(CL_String button)
{
    result = button;
    if(func_close().is_null() == false)
    {
        func_close().invoke();
    }
}

