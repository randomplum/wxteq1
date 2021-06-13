// wxWidgets "Hello World" Program
// For compilers that support precompilation, includes "wx/wx.h".
//#include <wx/wxprec.h>
//#ifndef WX_PRECOMP
//#include <wx/wx.h>
//#endif

#include "ThermalExpert.hpp"

class MyApp : public wxApp
{
public:
    virtual bool OnInit();
};
class MyFrame : public wxFrame
{
public:
    MyFrame();

private:
    void OnExit(wxCommandEvent &event);
    void OnAbout(wxCommandEvent &event);
    void OnTimer(wxTimerEvent &event);
    void OnButton(wxCommandEvent &event);
    void OnColourMapChange(wxCommandEvent &event);
    void OnBitmapPress(wxMouseEvent &event);

    ThermalExpert te;
    wxStaticBitmap *m_bitmap1;
    wxTimer m_timer;
    wxButton *m_button1;
    wxChoice *m_choice1;
    wxStaticText *m_staticText1;
    wxStaticBitmap *m_bitmap8;
    wxStaticText *m_staticText2;
};

wxIMPLEMENT_APP(MyApp);
bool MyApp::OnInit()
{
    MyFrame *frame = new MyFrame();
    frame->Show(true);
    return true;
}
MyFrame::MyFrame()
    : wxFrame(NULL, wxID_ANY, "TE-Q1", wxDefaultPosition)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);
    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    menuBar->Append(menuHelp, "&Help");
    SetMenuBar(menuBar);
    CreateStatusBar();

    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    wxBoxSizer *bSizer2;
    bSizer2 = new wxBoxSizer(wxVERTICAL);

    m_bitmap1 = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(384, 288), 0);
    m_bitmap1->SetMinSize(wxSize(384, 288));

    bSizer2->Add(m_bitmap1, 0, wxALL, 5);

    wxBoxSizer *bSizer4;
    bSizer4 = new wxBoxSizer(wxHORIZONTAL);

    m_staticText1 = new wxStaticText(this, wxID_ANY, wxT("-40.00℃"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText1->Wrap(-1);
    bSizer4->Add(m_staticText1, 0, wxALIGN_CENTER | wxALL, 5);

    m_bitmap8 = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(256, 16), 0);
    m_bitmap8->SetMinSize(wxSize(256, 16));

    bSizer4->Add(m_bitmap8, 0, wxALIGN_CENTER, 5);

    m_staticText2 = new wxStaticText(this, wxID_ANY, wxT("250.00℃"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText2->Wrap(-1);
    bSizer4->Add(m_staticText2, 0, wxALIGN_CENTER | wxALL, 5);

    bSizer2->Add(bSizer4, 1, wxALIGN_CENTER, 5);

    wxBoxSizer *bSizer3;
    bSizer3 = new wxBoxSizer(wxHORIZONTAL);

    m_button1 = new wxButton(this, wxID_ANY, wxT("Calibrate"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer3->Add(m_button1, 0, wxALIGN_CENTER|wxRIGHT, 5);

    wxString m_choice1Choices[] = {wxT("jet"), wxT("spring"), wxT("summer"), wxT("autumn"), wxT("winter"), wxT("cool"), wxT("hot"), wxT("bone"), wxT("copper"), wxT("afmhot"), wxT("terrain"), wxT("seismic"), wxT("magma"), wxT("inferno"), wxT("plasma"), wxT("viridis"), wxT("nipy_spectral"), wxT("hsv")};
    int m_choice1NChoices = sizeof(m_choice1Choices) / sizeof(wxString);
    m_choice1 = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, m_choice1NChoices, m_choice1Choices, 0);
    m_choice1->SetSelection(0);
    bSizer3->Add(m_choice1, 0, wxALIGN_CENTER|wxLEFT, 5);

    bSizer2->Add(bSizer3, 1, wxALIGN_CENTER, 5);

    this->SetSizer(bSizer2);
    this->Layout();
    bSizer2->Fit(this);

    this->Centre(wxBOTH);
    Bind(wxEVT_MENU, &MyFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MyFrame::OnExit, this, wxID_EXIT);

    if (te.Connect() != 0)
    {
        std::cout << "Could not connect to Thermal Expert" << std::endl;
        SetStatusText("Failed to connect");
    }
    else
    {
        std::cout << "Connected to Thermal Expert. Image size is " << te.ImageWidth() << "x" << te.ImageHeight() << std::endl;
        SetStatusText("Connected to TE-Q1");
        m_timer.Bind(wxEVT_TIMER, &MyFrame::OnTimer, this);
        m_button1->Bind(wxEVT_BUTTON, &MyFrame::OnButton, this);
        m_bitmap1->Bind(wxEVT_LEFT_DOWN, &MyFrame::OnBitmapPress, this);
        m_choice1->Bind(wxEVT_CHOICE, &MyFrame::OnColourMapChange, this);
        m_bitmap1->SetSize(te.ImageWidth(), te.ImageHeight());
        m_bitmap1->SetBitmap(*te.GetWxBitmap(m_choice1->GetString(m_choice1->GetCurrentSelection()).ToStdString()));
        m_bitmap1->Refresh();
        m_bitmap8->SetBitmap(*te.GetColourMapWxBitmap(m_choice1->GetString(m_choice1->GetCurrentSelection()).ToStdString()));
        m_bitmap8->Refresh();
        m_timer.Start(100);
    }
}
void MyFrame::OnExit(wxCommandEvent &event)
{
    te.Disconnect();
    Close(true);
}
void MyFrame::OnAbout(wxCommandEvent &event)
{
    wxMessageBox("This is a wxWidgets Hello World example",
                 "About Hello World", wxOK | wxICON_INFORMATION);
}

void MyFrame::OnTimer(wxTimerEvent &)
{
    m_bitmap1->SetBitmap(*te.GetWxBitmap(m_choice1->GetString(m_choice1->GetCurrentSelection()).ToStdString()));
    m_bitmap1->Refresh();
    m_staticText1->SetLabel(wxString::Format(wxT("%.2f℃"), te.getMinTemp()));
    m_staticText2->SetLabel(wxString::Format(wxT("%.2f℃"), te.getMaxTemp()));
    m_timer.Start(50);
}

void MyFrame::OnButton(wxCommandEvent &)
{
    te.DoNUC();
}

void MyFrame::OnBitmapPress(wxMouseEvent &event)
{
    wxString mystring = wxString::Format(wxT("%.2f℃"), te.getPointTemp(event.GetPosition().x, event.GetPosition().y));
    SetStatusText(mystring);
}

void MyFrame::OnColourMapChange(wxCommandEvent &event)
{
    m_bitmap8->SetBitmap(*te.GetColourMapWxBitmap(m_choice1->GetString(m_choice1->GetCurrentSelection()).ToStdString()));
    m_bitmap8->Refresh();
}