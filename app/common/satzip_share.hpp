#pragma once

#ifdef _WIN32
#include <string>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <shobjidl.h>

namespace sz {

inline bool share_file(HWND hwnd, const std::wstring& path) {
    using namespace winrt;
    using namespace winrt::Windows::ApplicationModel::DataTransfer;
    using namespace winrt::Windows::Storage;
    using namespace winrt::Windows::Foundation;
    try {
        auto interop = get_activation_factory<DataTransferManager, ::IDataTransferManagerInterop>();
        DataTransferManager dtm{ nullptr };
        check_hresult(interop->GetForWindow(hwnd, guid_of<DataTransferManager>(), put_abi(dtm)));

        static DataTransferManager s_last{ nullptr };
        static event_token s_tok{};
        if (s_last && s_tok.value) { try { s_last.DataRequested(s_tok); } catch (...) {} }

        std::wstring p = path;
        s_tok = dtm.DataRequested([p](DataTransferManager const&, DataRequestedEventArgs const& e) {
            auto req = e.Request();
            req.Data().Properties().Title(L"SAT-ZIP archive");
            auto def = req.GetDeferral();
            StorageFile::GetFileFromPathAsync(p).Completed(
                [req, def](IAsyncOperation<StorageFile> const& op, AsyncStatus st) {
                    try {
                        if (st == AsyncStatus::Completed) req.Data().SetStorageItems({ op.GetResults() });
                    } catch (...) {}
                    def.Complete();
                });
        });
        s_last = dtm;
        check_hresult(interop->ShowShareUIForWindow(hwnd));
        return true;
    } catch (...) { return false; }
}

}
#endif
