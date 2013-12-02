
#ifndef MessageDialog_h__
#define MessageDialog_h__



class MessageDialog : public CL_Window
{
    CL_Label *label;

    std::map<CL_String, CL_PushButton*> buttons;

    CL_String result;

public:
    enum DialogType { ASK_OK, ASK_OK_CANCEL, ASK_YES_NO, ASK_YES_NO_CANCEL, ASK_RETRY_ABORT, ASK_RETRY_ABORT_CANCEL };

    static const CL_String OK;
    static const CL_String CANCEL;
    static const CL_String YES;
    static const CL_String NO;
    static const CL_String RETRY;
    static const CL_String ABORT;

public:
    MessageDialog(CL_GUIComponent *owner, const CL_String &title, const CL_String &message, DialogType dialogType=ASK_OK);
    virtual ~MessageDialog();

    CL_GUIComponent * addButton(const CL_String &);
    CL_GUIComponent *getButton(const CL_String &);

    CL_String getResult() const;

private:
    void createDialog(DialogType dialogType);
    CL_GUITopLevelDescription getGUIDescription(const CL_String &title) const;
    bool onClose();
    void onClicked(CL_String button);

    void makeButtonGeneric(const CL_String &name, int width, int shiftFromCenter=0);
};



#endif // MessageDialog_h__


