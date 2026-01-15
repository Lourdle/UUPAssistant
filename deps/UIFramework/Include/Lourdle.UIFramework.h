#ifndef LOURDLE_UIFRAMEWORK_H
#define LOURDLE_UIFRAMEWORK_H

#include <Windows.h>
#include <CommCtrl.h>

#include <type_traits>
#include <tuple>

#define LOURDLE_UIFRAMEWORK_API __declspec(dllimport)


#undef SetWindowText
inline
BOOL SetWindowText(HWND hWnd, LPCTSTR lpString)
{
#ifdef UNICODE
	return ::SetWindowTextW(hWnd, lpString);
#else
	return ::SetWindowTextA(hWnd, lpString);
#endif
}


#undef GetWindowTextLength
inline
int GetWindowTextLength(HWND hWnd)
{
#ifdef UNICODE
	return ::GetWindowTextLengthW(hWnd);
#else
	return ::GetWindowTextLengthA(hWnd);
#endif
}


#undef GetWindowText
inline
int GetWindowText(HWND hWnd, LPTSTR lpString, int nMaxCount)
{
#ifdef UNICODE
	return ::GetWindowTextW(hWnd, lpString, nMaxCount);
#else
	return ::GetWindowTextA(hWnd, lpString, nMaxCount);
#endif
}


#undef GetMessage
inline
BOOL GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
#ifdef UNICODE
	return ::GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
#else
	return ::GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
#endif
}


#undef PeekMessage
inline
BOOL PeekMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
#ifdef UNICODE
	return ::PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
#else
	return ::PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
#endif
}


#undef SendMessage
inline
LRESULT SendMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
#ifdef UNICODE
	return ::SendMessageW(hWnd, Msg, wParam, lParam);
#else
	return ::SendMessageA(hWnd, Msg, wParam, lParam);
#endif
}


#undef PostMessage
inline
BOOL PostMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
#ifdef UNICODE
	return ::PostMessageW(hWnd, Msg, wParam, lParam);
#else
	return ::PostMessageA(hWnd, Msg, wParam, lParam);
#endif
}


#ifdef GetNextSibling
inline
HWND GetNextSibling(HWND hWnd)
{
	return ::GetWindow(hWnd, GW_HWNDNEXT);
}
#undef GetNextSibling
#endif


#ifdef GetPrevSibling
inline
HWND GetPrevSibling(HWND hWnd)
{
	return ::GetWindow(hWnd, GW_HWNDPREV);
}
#undef GetPrevSibling
#endif


#undef DrawText


#undef MessageBox
inline
int MessageBox(HWND hWnd, LPCTSTR lpText, LPCWSTR lpCaption, UINT uType)
{
#ifdef UNICODE
	return ::MessageBoxW(hWnd, lpText, lpCaption, uType);
#else
	return ::MessageBoxA(hWnd, lpText, lpCaption, uType);
#endif
}


#ifdef SHBrowseForFolder
#undef SHBrowseForFolder
#undef BROWSEINFO

#ifdef UNICODE
using BROWSEINFO = BROWSEINFOW;
#else
using BROWSEINFO = BROWSEINFOA;
#endif

#undef LPBROWSEINFO
using LPBROWSEINFO = BROWSEINFO*;
#undef PBROWSEINFO
using PBROWSEINFO = BROWSEINFO*;

inline
PIDLIST_ABSOLUTE SHBrowseForFolder(LPBROWSEINFO lpbi)
{
#ifdef UNICODE
	return ::SHBrowseForFolderW(lpbi);
#else
	return ::SHBrowseForFolderA(lpbi);
#endif
}
#endif


namespace Lourdle
{
	namespace UIFramework
	{
		/**
		* @brief Initializes the UI framework.
		* 
		* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
		*/
		LOURDLE_UIFRAMEWORK_API void UIFrameworkInit(bool bAllowDarkMode = true);

		/**
		* @brief Uninitializes the UI framework.
		*/
		LOURDLE_UIFRAMEWORK_API void UIFrameworkUninit();


		/**
		* @brief Generates a random number.
		* 
		* @return int The generated random number.
		*/
		int LOURDLE_UIFRAMEWORK_API Random();


		/**
		* @brief Represents a string.
		*/
		class LOURDLE_UIFRAMEWORK_API String
		{
		public:
			/**
			* @brief Default constructor.
			*/
			String();

			/**
			* @brief Copy constructor.
			*
			* @param other The string to copy from.
			*/
			String(const String& other);

			/**
			* @brief Move constructor.
			*
			* @param other The string to move from.
			*/
			String(String&& other);

			/**
			* @brief Constructs a string from a null-terminated wide character string.
			*
			* @param pSource The null-terminated wide character string.
			*/
			String(PCWSTR pSource);

			/**
			* @brief Constructs a string from a wide character string with a specified length.
			*
			* @param pSource The wide character string.
			* @param nLength The length of the string.
			*/
			String(PCWSTR pSource, SIZE_T nLength);

#ifdef _STRING_
			/**
			* @brief Constructs a string from a ::std::wstring object.
			*
			* @param ref The ::std::wstring object.
			*/
			template<class=void>
			String(const ::std::wstring& ref) : String(ref.c_str()) {}
#endif

			/**
			* @brief Destructor.
			*/
			~String();

			/**
			* @brief Gets the length of the string.
			*
			* @return SIZE_T The length of the string.
			*/
			SIZE_T GetLength() const;


			/**
			* @brief Gets the size of the memory region storing the string.
			*
			* @return SIZE_T The size of the memory region.
			*/
			SIZE_T GetSize() const;

			/**
			* @brief Resizes the memory region storing the string to the specified size.
			*
			* @param nSize The new size of the memory region.
			*/
			VOID Resize(SIZE_T nSize);


			/**
			* @brief Returns the string to a null-terminated wide character string.
			*
			* @return PCWSTR The null-terminated wide character string.
			*/
			operator PCWSTR() const;

#ifdef _STRING_
			/**
			* @brief Returns the string to a ::std::wstring object.
			*
			* @return ::std::wstring The ::std::wstring object.
			*/
			template<class = void>
			explicit operator ::std::wstring() const
			{
				return pString;
			}
#endif

			/**
			* @brief Gets a pointer to the internal buffer of the string.
			*
			* @return PWSTR A pointer to the internal buffer of the string.
			*/
			PWSTR GetPointer();


			/**
			* @brief Assigns a null-terminated wide character string to the string.
			*
			* @param pSource The null-terminated wide character string to assign.
			* @return String& The reference to the string after assignment.
			*/
			String& operator=(PCWSTR pSource);

			/**
			* @brief Assigns another string to the string.
			*
			* @param other The string to assign.
			* @return String& The reference to the string after assignment.
			*/
			String& operator=(const String& other);

			/**
			* @brief Moves another string to the string.
			*
			* @param other The string to move.
			* @return String& The reference to the string after move.
			*/
			String& operator=(String&& other);


#ifdef _STRING_
			/**
			* @brief Assigns a ::std::wstring to the string.
			*
			* @param ref The ::std::wstring object.
			*/
			template<class=void>
			String& operator=(const ::std::wstring& ref)
			{
				return operator=(ref.c_str());
			}
#endif

			/**
			* @brief Compares the string with a null-terminated wide character string.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the strings are equal, false otherwise.
			*/
			bool operator==(PCWSTR pString) const;

#ifdef _STRING_
			/**
			* @brief Compares the string with a ::std::wstring object.
			*
			* @param ref The ::std::wstring object to compare.
			* @return bool True if the strings are equal, false otherwise.
			*/
			template<class=void>
			bool operator==(const ::std::wstring& ref) const
			{
				return operator==(ref.c_str());
			}
#endif


			/**
			* @brief Compares the string with a null-terminated wide character string.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the strings are not equal, false otherwise.
			*/
			bool operator!=(PCWSTR pString) const;

#ifdef _STRING_
			/**
			* @brief Compares the string with a ::std::wstring object.
			*
			* @param ref The ::std::wstring object to compare.
			* @return bool True if the strings are not equal, false otherwise.
			*/
			template<class=void>
			bool operator!=(const ::std::wstring& ref) const
			{
				return operator!=(ref.c_str());
			}
#endif


			/**
			* brief Compares the string with a null-terminated wide character string.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the string is less than the specified string, false otherwise.
			*/
			bool operator<(PCWSTR pString) const;

			/**
			* @brief Compares the string with a null-terminated wide character string.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the string is more than the specified string, false otherwise.
			*/
			bool operator>(PCWSTR pString) const;

			/**
			* @brief Compares the string with a null-terminated wide character string.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the string is less than or equal to the specified string, false otherwise.
			*/
			bool operator<=(PCWSTR pString) const;

			/**
			* @brief Compares the string with a null-terminated wide character string.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the string is more than or equal to the specified string, false otherwise.
			*/
			bool operator>=(PCWSTR pString) const;


			/**
			* @brief Gets a reference to the character at the specified index in the string.
			*
			* @param index The index of the character.
			* @return WCHAR& A reference to the character at the specified index.
			*/
			WCHAR& operator[](SIZE_T index);

			/**
			* @brief Gets the character at the specified index in the string.
			*
			* @param index The index of the character.
			* @return WCHAR The character at the specified index.
			*/
			WCHAR operator[](SIZE_T index) const;


			/**
			* @brief Appends a character to the string.
			*
			* @param ch The character to append.
			* @return String& The reference to the string after appending.
			*/
			String& operator+=(WCHAR ch);

			/**
			* @brief Appends a null-terminated wide character string to the string.
			*
			* @param pString The null-terminated wide character string to append.
			* @return String& The reference to the string after appending.
			*/
			String& operator+=(PCWSTR pString);

			/**
			* @brief Appends another string to the string.
			*
			* @param other The string to append.
			* @return String& The reference to the string after appending.
			*/
			String& operator+=(const String& other);


			/**
			* @brief Concatenates a character to the string.
			*
			* @param ch The character to concatenate.
			* @return String The concatenated string.
			*/
			String operator+(WCHAR ch) const;

			/**
			* @brief Concatenates a null-terminated wide character string to the string.
			*
			* @param pString The null-terminated wide character string to concatenate.
			* @return String The concatenated string.
			*/
			String operator+(PCWSTR pString) const;

			/**
			* @brief Concatenates another string to the string.
			*
			* @param other The string to concatenate.
			* @return String The concatenated string.
			*/
			String operator+(const String& other) const;


			/**
			* @brief Gets an iterator to the beginning of the string.
			*
			* @return PWCHAR An iterator to the beginning of the string.
			*/
			PWCHAR begin();

			/**
			* @brief Gets an iterator to the end of the string.
			*
			* @return PWCHAR An iterator to the end of the string.
			*/
			PWCHAR end();

			/**
			* @brief Gets a constant iterator to the beginning of the string.
			*
			* @return PCWCHAR A constant iterator to the beginning of the string.
			*/
			PCWCHAR begin() const;

			/**
			* @brief Gets a constant iterator to the end of the string.
			*
			* @return PCWCHAR A constant iterator to the end of the string.
			*/
			PCWCHAR end() const;

			/**
			* @brief Checks if the string is empty.
			*
			* @return bool True if the string is empty, false otherwise.
			*/
			bool Empty() const;

			/**
			* @brief Compares the string with a null-terminated wide character string.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the strings are equal, false otherwise.
			*/
			bool Compare(PCWSTR pString) const;

			/**
			* @brief Compares the string with a null-terminated wide character string in a case-insensitive manner.
			*
			* @param pString The null-terminated wide character string to compare.
			* @return bool True if the strings are equal, false otherwise.
			*/
			bool CompareCaseInsensitive(PCWSTR pString) const;

		private:
			PWSTR pString;
		};


		/**
		* @brief Gets a string from the specified resource ID.
		* 
		* @param uResID The resource ID.
		* @param hModule The handle to the module containing the resource. Default is nullptr.
		* @return String The string from the resource.
		*/
		LOURDLE_UIFRAMEWORK_API String GetString(UINT uResID, HMODULE hModule = nullptr);


		enum class SizeType : bool
		{
			Width,
			Height
		};

		/**
		* @brief Gets the window size based on the specified size type, client size, window style, and menu presence.
		*
		* @param Type The size type (Width or Height).
		* @param nClientSize The client size of the window.
		* @param dwStyle The window style.
		* @param bHasMenu Whether the window has a menu or not. Default is false.
		* @return int The calculated window size.
		*/
		LOURDLE_UIFRAMEWORK_API int GetWindowSize(SizeType Type, int nClientSize, DWORD dwStyle, bool bHasMenu = false);

		/**
		* @brief Gets the font size based on the specified font and size type.
		*
		* @param hFont The font. Default is the system default font.
		* @param SizeType The size type (Width or Height). Default is Height.
		* @return int The calculated font size.
		*/
		LOURDLE_UIFRAMEWORK_API int GetFontSize(HFONT hFont = nullptr, SizeType SizeType = SizeType::Height);

		/**
		* @brief Gets the full size (ascent + descent) of the font based on the specified font and size type.
		*
		* @param hFont The font. Default is the system default font.
		* @param SizeType The size type (Width or Height). Default is Height.
		* @return int The calculated full size of the font.
		*/
		LOURDLE_UIFRAMEWORK_API int GetFontFullSize(HFONT hFont = nullptr, SizeType SizeType = SizeType::Height);


		/**
		* @brief Creates a font of the specified height using the system default font settings
		*
		* @param cHeight The height of the font.
		* @return HFONT The created font handle.
		*/
		LOURDLE_UIFRAMEWORK_API HFONT EzCreateFont(int cHeight);

		/**
		* @brief Creates a font using the system default font settings.
		*
		* @return HFONT The created font handle.
		*/
		LOURDLE_UIFRAMEWORK_API HFONT CreateSystemDefaultFont();


		/**
		* @brief Enters the message loop.
		*
		* @return int The exit code of the message loop.
		*/
		LOURDLE_UIFRAMEWORK_API int EnterMessageLoop();

		/**
		* @brief Enters the message loop and processes messages for the specified modeless dialog.
		*
		* @param hDlg The handle to the modeless dialog.
		* @return int The exit code of the message loop.
		*/
		LOURDLE_UIFRAMEWORK_API int EnterMessageLoop(HWND hDlg);

		/**
		* @brief Enters the message loop with a specified accelerator table.
		*
		* @param hAccel The handle to the accelerator table.
		* @param hWnd The handle to the window.
		* @return int The exit code of the message loop.
		*/
		LOURDLE_UIFRAMEWORK_API int EnterMessageLoop(HACCEL hAccel, HWND hWnd);

		/**
		* @brief Enters the message loop with a timeout.
		*
		* @param dwMilliseconds The timeout value in milliseconds.
		* @param hWnd The handle to the window. Default is nullptr.
		*/
		LOURDLE_UIFRAMEWORK_API void EnterMessageLoopTimeout(DWORD dwMilliseconds, HWND hWnd = nullptr);

		/**
		* @brief Dispatches all pending messages in the message queue.
		*
		* @param hWnd The handle to the window. Default is nullptr.
		*/
		LOURDLE_UIFRAMEWORK_API void DispatchAllMessages(HWND hWnd = nullptr);

		/**
		* @brief Gets the user theme color.
		*
		* @return COLORREF The user theme color. Fails to get the color, returns 0x0078D4.
		*/
		LOURDLE_UIFRAMEWORK_API COLORREF GetUserThemeColor();


		/**
		* @brief Shifts the specified rectangle to the down by the specified offset.
		*
		* @param Rect The rectangle to modify.
		* @param nOffset The offset value.
		* @return RECT& The modified rectangle.
		*/
		LOURDLE_UIFRAMEWORK_API RECT& operator+=(RECT& Rect, int nOffset);

		/**
		* @brief Shifts the specified rectangle to the up by the specified offset.
		*
		* @param Rect The rectangle to modify.
		* @param nOffset The offset value.
		* @return RECT& The modified rectangle.
		*/
		LOURDLE_UIFRAMEWORK_API RECT& operator-=(RECT& Rect, int nOffset);

		/**
		* @brief Shifts the specified rectangle to the right by the specified offset.
		*
		* @param Rect The rectangle to modify.
		* @param nOffset The offset value.
		* @return RECT& The modified rectangle.
		*/
		LOURDLE_UIFRAMEWORK_API RECT& operator>>=(RECT& Rect, int nOffset);

		/**
		* @brief Shifts the specified rectangle to the left by the specified offset.
		*
		* @param Rect The rectangle to modify.
		* @param nOffset The offset value.
		* @return RECT& The modified rectangle.
		*/
		LOURDLE_UIFRAMEWORK_API RECT& operator<<=(RECT& Rect, int nOffset);


		/**
		* @brief Represents a base class for windows in the UI framework.
		*/
		class LOURDLE_UIFRAMEWORK_API WindowBase
		{
		public:
			/**
			* @brief Default constructor. Deleted to prevent instantiation without a valid HWND.
			*/
			WindowBase() = delete;

			/**
			* @brief Copy constructor. Defaulted to allow copying of WindowBase objects.
			*
			* @param other The WindowBase object to copy.
			*/
			WindowBase(const WindowBase& other) = default;

			/**
			* @brief Move constructor. Defaulted to allow moving of WindowBase objects.
			*
			* @param other The WindowBase object to move.
			*/
			WindowBase(WindowBase&& other) = default;

			/**
			* @brief Constructs a WindowBase object with the specified HWND.
			*
			* @param hWnd The handle to the window.
			*/
			WindowBase(HWND hWnd);

			/**
			* @brief Equality comparison operator. Compares two WindowBase objects for equality.
			*
			* @param other The WindowBase object to compare.
			* @return bool True if the WindowBase objects are equal, false otherwise.
			*/
			bool operator==(const WindowBase& other) const;

			/**
			* @brief Equality comparison operator. Compares a WindowBase object with an HWND for equality.
			*
			* @param hWnd The handle to the window to compare.
			* @return bool True if the WindowBase object is equal to the HWND, false otherwise.
			*/
			bool operator==(HWND hWnd) const;

			/**
			* @brief Inequality comparison operator. Compares two WindowBase objects for inequality.
			*
			* @param other The WindowBase object to compare.
			* @return bool True if the WindowBase objects are not equal, false otherwise.
			*/
			bool operator!=(const WindowBase& other) const;

			/**
			* @brief Inequality comparison operator. Compares a WindowBase object with an HWND for inequality.
			*
			* @param hWnd The handle to the window to compare.
			* @return bool True if the WindowBase object is not equal to the HWND, false otherwise.
			*/
			bool operator!=(HWND hWnd) const;

			/**
			* @brief Conversion operator to bool. Checks if the WindowBase object is valid.
			*
			* @return bool True if the WindowBase object is valid, false otherwise.
			*/
			operator bool();

			/**
			* @brief Conversion operator to HWND. Retrieves the handle to the window.
			*
			* @return HWND The handle to the window.
			*/
			operator HWND() const;

			/**
			* @brief Gets the handle to the window.
			*
			* @return HWND The handle to the window.
			*/
			HWND GetHandle() const;

			/**
			* @brief Shows the window.
			*
			* @param nCmdShow The show command.
			* @return bool True if the window was shown successfully, false otherwise.
			*/
			bool ShowWindow(int nCmdShow);

			/**
			* @brief Shows the window asynchronously.
			*
			* @param nCmdShow The show command.
			* @return bool True if the window was shown asynchronously successfully, false otherwise.
			*/
			bool ShowWindowAsync(int nCmdShow);

			/**
			* @brief Invalidates a rectangular area within the window.
			*
			* @param lpRect The pointer to the rectangle to invalidate.
			* @param bErase Whether to erase the background of the invalidated area.
			* @return bool True if the area was invalidated successfully, false otherwise.
			*/
			bool InvalidateRect(LPCRECT lpRect, bool bErase = true);

			/**
			* @brief Invalidates the entire window.
			*
			* @param bErase Whether to erase the background of the invalidated area.
			* @return bool True if the window was invalidated successfully, false otherwise.
			*/
			bool Invalidate(bool bErase = true);

			/**
			* @brief Updates the window by repainting the invalidated areas.
			*
			* @return bool True if the window was updated successfully, false otherwise.
			*/
			bool UpdateWindow();

			/**
			* @brief Redraws the window.
			*
			* @param lprcUpdate The pointer to the rectangle to update. If nullptr, the entire window is updated.
			* @param hrgnUpdate The handle to the region to update. If nullptr, the entire window is updated.
			* @param uFlags The redraw flags.
			* @return bool True if the window was redrawn successfully, false otherwise.
			*/
			bool RedrawWindow(LPCRECT lprcUpdate, HRGN hrgnUpdate, UINT uFlags);

			/**
			* @brief Animates the window.
			*
			* @param dwTime The animation time in milliseconds.
			* @param dwFlags The animation flags.
			* @return bool True if the window was animated successfully, false otherwise.
			*/
			bool AnimateWindow(DWORD dwTime, DWORD dwFlags);

			/**
			* @brief Enables or disables the window.
			*
			* @param bEnable Whether to enable or disable the window.
			* @return bool True if the window was enabled or disabled successfully, false otherwise.
			*/
			bool EnableWindow(bool bEnable);

			/**
			* @brief Sets the window as the active window.
			*
			* @return bool True if the window was set as the active window successfully, false otherwise.
			*/
			bool SetActiveWindow();

			/**
			* @brief Sets the focus to the window.
			*
			* @return WindowBase If the function succeeds, the return value is the window that previously had the keyboard focus. If the this object is invalid or the window is not attached to the calling thread's message queue, the return value is an invalid object. To get extended error information, call GetLastError function.
			*/
			WindowBase SetFocus();

			/**
			* @brief Sets the window as the foreground window.
			*
			* Brings the thread that created the specified window into the foreground and activates the window.
			* Keyboard input is directed to the window, and various visual cues are changed for the user.
			* The system assigns a slightly higher priority to the thread that created the foreground window than it does to other threads.
			* 
			* @return bool True if the window was set as the foreground window successfully, false otherwise.
			*/
			bool SetForegroundWindow();

			/**
			* @brief Sets the text of the window.
			*
			* @param lpString The new window text.
			* @return bool True if the window text was set successfully, false otherwise.
			*/
			bool SetWindowText(LPCWSTR lpString);

			/**
			* @brief Gets the length of the window text.
			*
			* @return int The length of the window text.
			*/
			int GetWindowTextLength();

			/**
			* @brief Gets the window text.
			*
			* @param lpString The buffer to receive the window text.
			* @param nMaxCount The maximum number of characters to copy to the buffer.
			* @return int The number of characters copied to the buffer.
			*/
			int GetWindowText(LPWSTR lpString, int nMaxCount);

			/**
			* @brief Gets the window text.
			*
			* @return String The window text.
			*/
			String GetWindowText();

			/**
			* @brief Gets the client rectangle of the window.
			*
			* @param pRect The pointer to the rectangle to receive the client rectangle.
			* @return bool True if the client rectangle was retrieved successfully, false otherwise.
			*/
			bool GetClientRect(PRECT pRect) const;

			/**
			* @brief Gets the window rectangle.
			*
			* @param pRect The pointer to the rectangle to receive the window rectangle.
			* @return bool True if the window rectangle was retrieved successfully, false otherwise.
			*/
			bool GetWindowRect(PRECT pRect) const;

			/**
			* @brief Gets the thread and process IDs associated with the window.
			*
			* @param lpdwProcessId The pointer to the variable to receive the process ID.
			* @return DWORD The thread ID.
			*/
			DWORD GetWindowThreadProcessId(LPDWORD lpdwProcessId) const;

			/**
			* @brief Retrieves a message from the window's message queue.
			*
			* @param lpMsg The pointer to the MSG structure that receives the message.
			* @param wMsgFilterMin The minimum value of the message range.
			* @param wMsgFilterMax The maximum value of the message range.
			* @return bool True if a message was retrieved successfully, false otherwise.
			*/
			bool GetMessage(LPMSG lpMsg, UINT wMsgFilterMin, UINT wMsgFilterMax);

			/**
			* @brief Retrieves a message from the window's message queue without removing it.
			*
			* @param lpMsg The pointer to the MSG structure that receives the message.
			* @param wMsgFilterMin The minimum value of the message range.
			* @param wMsgFilterMax The maximum value of the message range.
			* @param wRemoveMsg The removal flags.
			* @return bool True if a message was retrieved successfully, false otherwise.
			*/
			bool PeekMessage(LPMSG lpMsg, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);

			/**
			* @brief Sends the specified message to the window.
			*
			* @param Msg The message to send.
			* @param wParam The WPARAM parameter of the message.
			* @param lParam The LPARAM parameter of the message.
			* @return LRESULT The result of the message processing.
			*/
			LRESULT SendMessage(UINT Msg, WPARAM wParam, LPARAM lParam);

			/**
			* @brief Posts the specified message to the window.
			*
			* @param Msg The message to post.
			* @param wParam The WPARAM parameter of the message.
			* @param lParam The LPARAM parameter of the message.
			* @return bool True if the message was posted successfully, false otherwise.
			*/
			bool PostMessage(UINT Msg, WPARAM wParam, LPARAM lParam);

			/**
			* @brief Dispatches all pending messages in the window's message queue.
			*/
			void DispatchAllMessages();

			/**
			* @brief Sets the position and size of the window.
			*
			* @param hWndInsertAfter The handle to the window to insert after.
			* @param X The new x-coordinate of the window.
			* @param Y The new y-coordinate of the window.
			* @param cx The new width of the window.
			* @param cy The new height of the window.
			* @param uFlags The window positioning flags.
			* @return bool True if the window position and size were set successfully, false otherwise.
			*/
			bool SetWindowPos(HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);

			/**
			* @brief Sets the position and size of the window.
			*
			* @param WndInsertAfter The WindowBase object to insert after.
			* @param X The new x-coordinate of the window.
			* @param Y The new y-coordinate of the window.
			* @param cx The new width of the window.
			* @param cy The new height of the window.
			* @param uFlags The window positioning flags.
			* @return bool True if the window position and size were set successfully, false otherwise.
			*/
			bool SetWindowPos(WindowBase* WndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);

			/**
			* @brief Sets the position and size of the window.
			*
			* @param X The new x-coordinate of the window.
			* @param Y The new y-coordinate of the window.
			* @param cx The new width of the window.
			* @param cy The new height of the window.
			* @param uFlags The window positioning flags.
			* @return bool True if the window position and size were set successfully, false otherwise.
			*/
			bool SetWindowPos(int X, int Y, int cx, int cy, UINT uFlags);

			/**
			* @brief Moves the window to the specified position and size.
			*
			* @param X The new x-coordinate of the window.
			* @param Y The new y-coordinate of the window.
			* @param nWidth The new width of the window.
			* @param nHeight The new height of the window.
			* @param bRepaint Whether to repaint the window after moving.
			* @return bool True if the window was moved successfully, false otherwise.
			*/
			bool MoveWindow(int X, int Y, int nWidth, int nHeight, bool bRepaint = true);

			/**
			* @brief Centers the window on the screen.
			*
			* @return bool True if the window was centered successfully, false otherwise.
			*/
			bool CenterWindow();

			/**
			* @brief Centers the window within the specified rectangle.
			*
			* @param Rect The rectangle to center the window within.
			* @return bool True if the window was centered successfully, false otherwise.
			*/
			bool CenterWindow(const RECT& Rect);

			/**
			* @brief Centers the window within the rectangle of specified window.
			*
			* @param hWnd The handle to the window to center the window within.
			* @return bool True if the window was centered successfully, false otherwise.
			*/
			bool CenterWindow(HWND hWnd);

			/**
			* @brief Centers the window within the rectangle of specified window.
			*
			* @param pWindowBase The WindowBase object to center the window within.
			* @return bool True if the window was centered successfully, false otherwise.
			*/
			bool CenterWindow(const WindowBase* pWindowBase);

			/**
			* @brief Adds a window style to the window.
			*
			* @param dwStyle The window style to add.
			* @return bool True if the window style was added successfully, false otherwise.
			*/
			bool AddWindowStyle(DWORD dwStyle);

			/**
			* @brief Removes a window style from the window.
			*
			* @param dwStyle The window style to remove.
			* @return bool True if the window style was removed successfully, false otherwise.
			*/
			bool RemoveWindowStyle(DWORD dwStyle);

			/**
			* @brief Gets the window style of the window.
			*
			* @return DWORD The window style of the window.
			*/
			DWORD GetWindowStyle() const;

			/**
			* @brief Sets the window style of the window.
			*
			* @param dwStyle The new window style.
			* @return bool True if the window style was set successfully, false otherwise.
			*/
			bool SetWindowStyle(DWORD dwStyle);

			/**
			* @brief Retrieves a handle to a window that has the specified relationship (Z-Order or owner) to the specified window.
			*
			* @param uCmd The relationship between the specified window and the window whose handle is to be retrieved.
			* @return WindowBase The window with the specified command ID.
			*/
			WindowBase GetWindow(UINT uCmd);

			/**
			* @brief Gets the next sibling window.
			*
			* @return WindowBase The next sibling window.
			*/
			WindowBase GetNextSibling();

			/**
			* @brief Gets the previous sibling window.
			*
			* @return WindowBase The previous sibling window.
			*/
			WindowBase GetPrevSibling();

			/**
			* @brief Adds an extended window style to the window.
			*
			* @param dwExtendedStyle The extended window style to add.
			* @return bool True if the extended window style was added successfully, false otherwise.
			*/
			bool AddExtendedWindowStyle(DWORD dwExtendedStyle);

			/**
			* @brief Removes an extended window style from the window.
			*
			* @param dwExtendedStyle The extended window style to remove.
			* @return bool True if the extended window style was removed successfully, false otherwise.
			*/
			bool RemoveExtendedWindowStyle(DWORD dwExtendedStyle);

			/**
			* @brief Gets the extended window style of the window.
			*
			* @return DWORD The extended window style of the window.
			*/
			DWORD GetExtendedWindowStyle() const;

			/**
			* @brief Sets the extended window style of the window.
			*
			* @param dwExtendedStyle The new extended window style.
			* @return bool True if the extended window style was set successfully, false otherwise.
			*/
			bool SetExtendedWindowStyle(DWORD dwExtendedStyle);

			/**
			* @brief Sets the attributes of a layered window.
			*
			* @param crKey The color key.
			* @param bAlpha The alpha value.
			* @param dwFlags The flags.
			* @return bool True if the layered window attributes were set successfully, false otherwise.
			*/
			bool SetLayeredWindowAttributes(COLORREF crKey, BYTE bAlpha, DWORD dwFlags);

			/**
			* @brief Sets the window region.
			*
			* @param hRgn The handle to the region.
			* @param bRedraw Whether to redraw the window after setting the region.
			* @return int The return value of the SetWindowRgn function.
			*/
			int SetWindowRgn(HRGN hRgn, bool bRedraw = true);

			/**
			* @brief Closes the window.
			*
			* @return bool True if the window was closed successfully, false otherwise.
			*/
			bool CloseWindow();

			/**
			* @brief Destroys the window.
			*
			* @return bool True if the window was destroyed successfully, false otherwise.
			*/
			bool DestroyWindow();

			/**
			* @brief Sets the parent window.
			*
			* @param NewParent The new parent window.
			* @return WindowBase The WindowBase object after setting the parent window.
			*/
			WindowBase SetParent(WindowBase* NewParent);

			/**
			* @brief Checks if the window is valid.
			*
			* @return bool True if the window is valid, false otherwise.
			*/
			bool IsWindow() const;

			/**
			* @brief Checks if the window is visible.
			*
			* @return bool True if the window is visible, false otherwise.
			*/
			bool IsWindowVisible() const;

			/**
			* @brief Checks if the window is enabled.
			*
			* @return bool True if the window is enabled, false otherwise.
			*/
			bool IsWindowEnabled() const;

		protected:
			HWND hWnd;
		};


		/**
		* @brief Draws formatted text in the specified rectangle.
		*
		* @param hDC The device context.
		* @param lpChText The null-terminated wide character string to draw.
		* @param cchText The length of the string. If -1, the string is assumed to be null-terminated.
		* @param lprc The rectangle in which to draw the text.
		* @param format The formatting options.
		* @return int The height of the drawn text.
		*/
		LOURDLE_UIFRAMEWORK_API int DrawText(HDC hDC, LPCWSTR lpChText, int cchText, LPRECT lprc, UINT format);

		/**
		* @brief Draws formatted text in the specified rectangle.
		*
		* @param hDC The device context.
		* @param lpChText The null-terminated wide character string to draw.
		* @param lprc The rectangle in which to draw the text.
		* @param format The formatting options.
		* @return int The height of the drawn text.
		*/
		LOURDLE_UIFRAMEWORK_API int DrawText(HDC hDC, LPCWSTR lpChText, LPRECT lprc, UINT format);

		/**
		* @brief Draws formatted text from a resource in the specified rectangle.
		*
		* @param hDC The device context.
		* @param uResID The resource ID of the string to draw.
		* @param lpRect The rectangle in which to draw the text.
		* @param Format The formatting options.
		* @param hModule The handle to the module containing the resource. Default is nullptr.
		* @return int The height of the drawn text.
		*/
		LOURDLE_UIFRAMEWORK_API int DrawText(HDC hDC, UINT uResID, LPRECT lpRect, UINT Format, HMODULE hModule = nullptr);


		/**
		* @brief Checks if a color is considered light.
		*
		* @param Color The color to check.
		* @return bool True if the color is considered light, false otherwise.
		*/
		constexpr
			bool IsColorLight(COLORREF Color)
		{
			return (((5 * GetGValue(Color)) + (2 * GetRValue(Color)) + GetBValue(Color)) > (8 * 128));
		}

		/**
		* @brief Inverts the specified color.
		*
		* @param Color The color to invert.
		* @return COLORREF The inverted color.
		*/
		constexpr
			COLORREF InvertColor(COLORREF Color)
		{
			return RGB(255 - GetRValue(Color), 255 - GetGValue(Color), 255 - GetBValue(Color));
		}

		/**
		* @brief The background color for dark mode.
		*/
		constexpr COLORREF DarkBkColor = 0x202020;

		/**
		* @brief The text color for dark mode.
		*/
		constexpr COLORREF DarkTextColor = 0xFFFFFF;

		/**
		* @brief Selects an object into the specified device context.
		*
		* @tparam HGDIOBJ The type of the object to select.
		* @param hDC The device context.
		* @param hObject The object to select.
		* @return HGDIOBJ The previously selected object.
		*/
		template<typename HGDIOBJ>
		inline
			HGDIOBJ SelectObject(HDC hDC, HGDIOBJ hObject)
		{
			return reinterpret_cast<HGDIOBJ>(::SelectObject(hDC, hObject));
		}


		/**
		* @brief Represents a scroll bar.
		*/
		struct ScrollBar;

		/**
		* @brief Represents a batch positioner for window positioning.
		*/
		class LOURDLE_UIFRAMEWORK_API WindowBatchPositioner
		{
			DWORD_PTR Internal;

		public:
			/**
			* @brief Constructs a WindowBatchPositioner object with the specified offsets.
			*
			* @param nXOffset The X offset.
			* @param nYOffset The Y offset.
			*/
			WindowBatchPositioner(int nXOffset = 0, int nYOffset = 0);

			/**
			* @brief Deleted copy constructor.
			*/
			WindowBatchPositioner(const WindowBatchPositioner&) = delete;

			/**
			* @brief Move constructor.
			*
			* @param other The WindowBatchPositioner to move from.
			*/
			WindowBatchPositioner(WindowBatchPositioner&& other) noexcept;

			/**
			* @brief Destructor.
			*/
			~WindowBatchPositioner();

			/**
			* @brief Moves the specified window by the offset.
			*
			* @param hWnd The handle to the window.
			* @param hWndInsertAfter The handle to the window to position the window after.
			* @param X The new X position.
			* @param Y The new Y position.
			* @param cx The new width.
			* @param cy The new height.
			* @param uFlags The window positioning flags.
			* @return bool True if the window was moved successfully, false otherwise.
			*/
			bool SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);

			/**
			* @brief Moves the specified window by the offset.
			*
			* @param hWnd The handle to the window.
			* @param X The new X position.
			* @param Y The new Y position.
			* @param cx The new width.
			* @param cy The new height.
			* @param bRepaint Whether to repaint the window after moving. Default is true.
			* @return bool True if the window was moved successfully, false otherwise.
			*/
			bool MoveWindow(HWND hWnd, int X, int Y, int cx, int cy, bool bRepaint = true);

			/**
			* @brief Ends the deferred window positioning.
			*
			* @return bool True if the deferred window positioning was ended successfully, false otherwise.
			*/
			bool EndDeferWindowPos();

			int nXOffset; /**< The X offset. */
			int nYOffset; /**< The Y offset. */
		};




#define INVALID_HBRUSH reinterpret_cast<HBRUSH>(ULONG_PTR(-1)) // When Window::hbrDarkBackground is INVALID_HBRUSH, it means that the window does not need to support dark mode.
		

		/**
		* @brief Represents a window in the UI framework.
		*/
		class LOURDLE_UIFRAMEWORK_API Window : public WindowBase
		{
		public:

			/**
			* @brief Deleted copy constructor.
			*/
			Window(const Window&) = delete;

			/**
			* @brief Deleted move constructor.
			*/
			Window(Window&&) = delete;

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param dwExStyle The extended window style.
			* @param nClientWidth The width of the client area.
			* @param nClientHeight The height of the client area.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle. Default is nullptr.
			* @param hMenu The menu handle. Default is nullptr.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			Window(DWORD dwExStyle, int nClientWidth, int nClientHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent = nullptr, HMENU hMenu = nullptr, bool bAllowDarkMode = true);

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param nClientWidth The width of the client area.
			* @param nClientHeight The height of the client area.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle. Default is nullptr.
			* @param hMenu The menu handle. Default is nullptr.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			Window(int nClientWidth, int nClientHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent = nullptr, HMENU hMenu = nullptr, bool bAllowDarkMode = true);

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param dwExStyle The extended window style.
			* @param nClientWidth The width of the client area.
			* @param nClientHeight The height of the client area.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle.
			* @param hIcon The icon handle.
			* @param hSmallIcon The small icon handle.
			* @param hCursor The cursor handle.
			* @param hbrBackground The background brush handle.
			* @param hbrDarkBackground The dark background brush handle.
			* @param hMenu The menu handle.
			* @param hFont The font handle.
			*/
			Window(DWORD dwExStyle, int nClientWidth, int nClientHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent, HICON hIcon, HICON hSmallIcon, HCURSOR hCursor, HBRUSH hbrBackground, HBRUSH hbrDarkBackground, HMENU hMenu, HFONT hFont);

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param nClientWidth The width of the client area.
			* @param nClientHeight The height of the client area.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle.
			* @param hIcon The icon handle.
			* @param hSmallIcon The small icon handle.
			* @param hCursor The cursor handle.
			* @param hbrBackground The background brush handle.
			* @param hbrDarkBackground The dark background brush handle.
			* @param hMenu The menu handle.
			* @param hFont The font handle.
			*/
			Window(int nClientWidth, int nClientHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent, HICON hIcon, HICON hSmallIcon, HCURSOR hCursor, HBRUSH hbrBackground, HBRUSH hbrDarkBackground, HMENU hMenu, HFONT hFont);

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param dwExStyle The extended window style.
			* @param X The x-coordinate of the window position.
			* @param Y The y-coordinate of the window position.
			* @param nWidth The width of the window.
			* @param nHeight The height of the window.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle. Default is nullptr.
			* @param hMenu The menu handle. Default is nullptr.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			Window(DWORD dwExStyle, int X, int Y, int nWidth, int nHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent = nullptr, HMENU hMenu = nullptr, bool bAllowDarkMode = true);

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param X The x-coordinate of the window position.
			* @param Y The y-coordinate of the window position.
			* @param nWidth The width of the window.
			* @param nHeight The height of the window.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle. Default is nullptr.
			* @param hMenu The menu handle. Default is nullptr.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			Window(int X, int Y, int nWidth, int nHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent = nullptr, HMENU hMenu = nullptr, bool bAllowDarkMode = true);

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param dwExStyle The extended window style.
			* @param X The x-coordinate of the window position.
			* @param Y The y-coordinate of the window position.
			* @param nWidth The width of the window.
			* @param nHeight The height of the window.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle.
			* @param hIcon The icon handle.
			* @param hSmallIcon The small icon handle.
			* @param hCursor The cursor handle.
			* @param hbrBackground The background brush handle.
			* @param hbrDarkBackground The dark background brush handle.
			* @param hMenu The menu handle.
			* @param hFont The font handle.
			*/
			Window(DWORD dwExStyle, int X, int Y, int nWidth, int nHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent, HICON hIcon, HICON hSmallIcon, HCURSOR hCursor, HBRUSH hbrBackground, HBRUSH hbrDarkBackground, HMENU hMenu, HFONT hFont);

			/**
			* @brief Constructs a window with the specified parameters.
			*
			* @param X The x-coordinate of the window position.
			* @param Y The y-coordinate of the window position.
			* @param nWidth The width of the window.
			* @param nHeight The height of the window.
			* @param lpWindowName The name of the window.
			* @param dwStyle The window style.
			* @param hParent The parent window handle.
			* @param hIcon The icon handle.
			* @param hSmallIcon The small icon handle.
			* @param hCursor The cursor handle.
			* @param hbrBackground The background brush handle.
			* @param hbrDarkBackground The dark background brush handle.
			* @param hMenu The menu handle.
			* @param hFont The font handle.
			*/
			Window(int X, int Y, int nWidth, int nHeight, LPCWSTR lpWindowName, DWORD dwStyle, HWND hParent, HICON hIcon, HICON hSmallIcon, HCURSOR hCursor, HBRUSH hbrBackground, HBRUSH hbrDarkBackground, HMENU hMenu, HFONT hFont);

			/**
			* @brief Destructor.
			*/
			virtual ~Window();


			/**
			* @brief Whether to use double buffering for the window.
			*/
			UINT bDoubleBuffer : 1;

			/**
			* @brief The cursor to be displayed when the mouse is over the window.
			*/
			HCURSOR hCursor;

			/**
			* @brief The background brush for the window.
			*/
			HBRUSH hbrBackground;

			/**
			* @brief The dark background brush for the window in dark mode.
			*/
			HBRUSH hbrDarkBackground;

			/**
			* @brief The font used for text rendering in the window.
			*/
			HFONT hFont;

			/**
			* @brief Gets the icon of the window based on the specified DPI.
			*
			* @param uDPI The DPI value.
			* @return HICON The icon of the window.
			*/
			HICON GetIcon(UINT uDPI);

			/**
			* @brief Sets the icon of the window.
			*
			* @param hIcon The icon handle.
			* @return HICON The previous icon handle.
			*/
			HICON SetIcon(HICON hIcon);

			/**
			* @brief Gets the small icon of the window based on the specified DPI.
			*
			* @param uDPI The DPI value.
			* @return HICON The small icon of the window.
			*/
			HICON GetSmallIcon(UINT uDPI);

			/**
			* @brief Sets the small icon of the window.
			*
			* @param hSmallIcon The small icon handle.
			* @return HICON The previous small icon handle.
			*/
			HICON SetSmallIcon(HICON hSmallIcon);

			/**
			* @brief Structure representing a command.
			*/
			struct CommandStruct
			{
				HWND hWnd; /**< The handle to the control. */
				WORD wID; /**< The ID of the control. */
				WORD wCode; /**< The code of the command. */
			};

			/**
			* @brief Registers a command with a command procedure and associated data.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			* @param lParam The associated data.
			* @param Object The object associated with the command procedure.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(PVOID Object, LPARAM lParam), LPARAM lParam, PVOID Object);

			/**
			* @brief Registers a command with a command procedure and associated data.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			* @param lParam The associated data.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(Window*, LPARAM), LPARAM lParam);

			/**
			* @brief Registers a command with a command procedure.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(Window*));

			/**
			* @brief Registers a command with a command procedure and associated data.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			* @param lParam The associated data.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(HWND, LPARAM), LPARAM lParam);

			/**
			* @brief Registers a command with a command procedure.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(HWND));

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam C The type of the object.
			* @tparam T The type of the return value of the command procedure.
			* @tparam U The type of the argument passed to the command procedure.
			* @param CmdProc The command procedure.
			* @param Obj The pointer to the object associated with the command procedure.
			* @param Arg The argument passed to the command procedure.
			* @param hWnd The handle to the control.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<class C, class CArg, typename T, typename U, typename UArg>
			::std::enable_if_t<sizeof(U) == sizeof(LPARAM)>
				RegisterCommand(T(C::* CmdProc)(U), CArg*&& Obj, UArg&& Arg, HWND hWnd, WORD wID, WORD wNotifyCode)
			{
				U convArg = ::std::forward<UArg>(Arg);
				RegisterCommand({ hWnd, wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), *reinterpret_cast<LPARAM*>(&convArg),
					::std::forward<C*>(Obj));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam C The type of the object.
			* @tparam T The type of the return value of the command procedure.
			* @param CmdProc The command procedure.
			* @param Obj The pointer to the object associated with the command procedure.
			* @param hWnd The handle to the control.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<class C, class CArg, typename T>
			void RegisterCommand(T(C::* CmdProc)(), CArg*&& Obj, HWND hWnd, WORD wID, WORD wNotifyCode)
			{
				RegisterCommand({ hWnd, wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0,
					::std::forward<CArg*>(Obj));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam T The type of the return value of the command procedure.
			* @param CmdProc The command procedure.
			* @param hWnd The handle to the control.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<typename T>
			void RegisterCommand(T(*CmdProc)(), HWND hWnd, WORD wID, WORD wNotifyCode)
			{
				RegisterCommand({ hWnd, wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0, nullptr);
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam T The type of the return value of the command procedure.
			* @tparam U The type of the argument passed to the command procedure.
			* @param CmdProc The command procedure.
			* @param Arg The argument passed to the command procedure.
			* @param hWnd The handle to the control.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<typename T, typename U, typename UArg>
			::std::enable_if_t<sizeof(U) == sizeof(PVOID)>
				RegisterCommand(T(*CmdProc)(U), UArg&& Arg, HWND hWnd, WORD wID, WORD wNotifyCode)
			{
				U convArg = ::std::forward<UArg>(Arg);
				RegisterCommand({ hWnd, wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0,
					*reinterpret_cast<PVOID>(&convArg));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam T The type of the return value of the command procedure.
			* @tparam U The type of the 1st argument passed to the command procedure.
			* @tparam V The type of the 2nd argument passed to the command procedure.
			* @param CmdProc The command procedure.
			* @param Arg The argument passed to the command procedure.
			* @param hWnd The handle to the control.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<typename T, typename U, typename V, typename UArg, typename VArg>
			::std::enable_if_t<sizeof(U) == sizeof(PVOID) && sizeof(V) == sizeof(LPARAM)>
				RegisterCommand(T(*CmdProc)(U, V), UArg&& Arg1, VArg&& Arg2, HWND hWnd, WORD wID, WORD wNotifyCode)
			{
				U convArg1 = ::std::forward<UArg>(Arg1);
				V convArg2 = ::std::forward<VArg>(Arg2);
				RegisterCommand({ hWnd, wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc),
					*reinterpret_cast<LPARAM*>(&convArg2),
					*reinterpret_cast<PVOID*>(&convArg1));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam Wnd The type of the window class.
			* @param CmdProc The command procedure.
			* @param hWnd The handle to the control.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<class Wnd>
			::std::enable_if_t<::std::is_base_of<Window, Wnd>::value>
				RegisterCommand(void(Wnd::* CmdProc)(), HWND hWnd, WORD wID, WORD wNotifyCode)
			{
				auto p = dynamic_cast<Wnd*>(this);
				if (p)
					RegisterCommand({ hWnd, wID, wNotifyCode },
						*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0, p);
			}

			/**
			* @brief Unregisters a command.
			*
			* @param CmdStruct The command structure.
			*/
			void UnregisterCommand(const CommandStruct& CmdStruct);

			/**
			* @brief Unregisters a command.
			*
			* Unregisters all commands associated with the specified window.
			*
			* @param hWnd The handle to the window.
			*/
			void UnregisterCommand(HWND hWnd);

			/**
			* @brief Posts a command message
			*
			* @param hWnd The handle to the window.
			* @param wID The command identifier.
			* @param wCode The command code.
			* @return bool True if the command message was posted successfully, false otherwise.
			*/
			bool PostCommand(HWND hWnd, WORD wID, WORD wCode);

			/**
			* @brief Sends a command message
			*
			* @param hWnd The handle to the window.
			* @param wID The command identifier.
			* @param wCode The command code.
			* @return bool True if the command message was sent successfully, false otherwise.
			*/
			bool SendCommand(HWND hWnd, WORD wID, WORD wCode);

			/**
			* @brief Sets a timer for the specified window.
			*
			* @param nIDEvent The timer identifier.
			* @param uElapse The time interval between timer notifications, in milliseconds.
			* @param lpTimerProc A pointer to the timer callback function.
			* @return bool True if the timer was set successfully, false otherwise.
			*/
			bool SetTimer(UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerProc);

			/**
			* @brief Kills the specified timer.
			*
			* @param nIDEvent The timer identifier.
			* @return bool True if the timer was killed successfully, false otherwise.
			*/
			bool KillTimer(UINT_PTR nIDEvent);

			/**
			* @brief Retrieves a pointer to the Window object associated with the specified window handle.
			*
			* @param hWnd The handle to the window.
			* @return Window* A pointer to the Window object associated with the window handle.
			*/
			static Window* GetObjectPointer(HWND hWnd);

			/**
			* @brief Retrieves a pointer to the specified derived Window object associated with the specified window handle.
			*
			* @tparam Wnd The derived Window class.
			* @param hWnd The handle to the window.
			* @return Wnd* A pointer to the specified derived Window object associated with the window handle.
			* @throws std::bad_cast if the specified derived Window class is not valid.
			*/
			template<class Wnd>
			static Wnd* GetObjectPointer(HWND hWnd)
			{
				static_assert(::std::is_base_of<Window, Wnd>::value, "Invalid template parameter. The template parameter must be a class derived from Window.");
				return dynamic_cast<Wnd*>(GetObjectPointer(hWnd));
			}

			/**
			* @brief Displays a message box with the specified text and caption.
			*
			* @param lpText The text to display in the message box.
			* @param lpCaption The caption of the message box.
			* @param uType The type of the message box.
			* @return int The result of the message box.
			*/
			int MessageBox(LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);


#ifdef SHFOLDERAPI
			/**
			* @brief Displays a dialog box that enables the user to select a folder.
			*
			* @param lpbi A pointer to a BROWSEINFO structure that contains information used to display the dialog box.
			* @return PIDLIST_ABSOLUTE A pointer to an item identifier list (PIDL) specifying the location of the selected folder, or nullptr if the user cancels the dialog box or an error occurs.
			*/
			LPITEMIDLIST SHBrowseForFolder(LPBROWSEINFOW lpbi);
#endif

			/**
			* @brief Displays an error message box with the specified type.
			*
			* @param uType The type of the error message box.
			* @return int The result of the error message box.
			*/
			int ErrorMessageBox(UINT uType = MB_ICONERROR);

			/**
			* @brief Enables or disables the specified scroll bar.
			*
			* @param wSBflags The scroll bar flags.
			* @param wArrows The scroll bar arrows.
			* @return bool True if the scroll bar is enabled or disabled successfully, false otherwise.
			*/
			bool EnableScrollBar(
				UINT wSBflags,
				UINT wArrows
			);

			/**
			* @brief Retrieves information about the specified scroll bar.
			*
			* @param idObject The identifier of the scroll bar.
			* @param psbi A pointer to a SCROLLBARINFO structure that receives the information.
			* @return bool True if the information is retrieved successfully, false otherwise.
			*/
			bool GetScrollBarInfo(
				LONG idObject,
				PSCROLLBARINFO psbi
			);

			/**
			* @brief Retrieves the parameters of a scroll bar, including the minimum and maximum scrolling positions, the page size, and the position of the scroll box (thumb).
			*
			* @param nBar The type of scroll bar.
			* @param lpsi A pointer to a SCROLLINFO structure that receives the parameters.
			* @return bool True if the parameters are retrieved successfully, false otherwise.
			*/
			bool GetScrollInfo(
				int nBar,
				LPSCROLLINFO lpsi
			);

			/**
			* @brief Retrieves the current position of the scroll box (thumb) in the specified scroll bar.
			*
			* @param nBar The type of scroll bar.
			* @return int The current position of the scroll box.
			*/
			int GetScrollPos(
				int nBar
			);

			/**
			* @brief Retrieves the minimum and maximum scrolling positions for the specified scroll bar.
			*
			* @param nBar The type of scroll bar.
			* @param lpMinPos A pointer to the variable that receives the minimum scrolling position.
			* @param lpMaxPos A pointer to the variable that receives the maximum scrolling position.
			* @return bool True if the minimum and maximum positions are retrieved successfully, false otherwise.
			*/
			bool GetScrollRange(
				int nBar,
				LPINT lpMinPos,
				LPINT lpMaxPos
			);

			/**
			* @brief Retrieves the page size of the specified scroll bar.
			*
			* @param nBar The type of scroll bar.
			* @return UINT The page size of the scroll bar.
			*/
			UINT GetScrollPage(
				int nBar
			);

			/**
			* @brief Scrolls the contents of the specified window.
			*
			* @param dx The amount of horizontal scrolling.
			* @param dy The amount of vertical scrolling.
			* @param lprcScroll A pointer to a RECT structure that specifies the portion of the window to be scrolled.
			* @param lprcClip A pointer to a RECT structure that specifies the portion of the window to be clipped.
			* @return bool True if the window is scrolled successfully, false otherwise.
			*/
			bool ScrollWindow(
				int dx,
				int dy,
				LPCRECT lprcScroll = nullptr,
				LPCRECT lprcClip = nullptr
			);

			/**
			* @brief Scrolls the contents of the specified window.
			*
			* @param dx The amount of horizontal scrolling.
			* @param dy The amount of vertical scrolling.
			* @param lprcScroll A pointer to a RECT structure that specifies the portion of the window to be scrolled.
			* @param lprcClip A pointer to a RECT structure that specifies the portion of the window to be clipped.
			* @param hrgnUpdate A handle to the region that is modified to hold the region invalidated by scrolling.
			* @param lprcUpdate A pointer to a RECT structure that receives the boundaries of the modified region.
			* @param flags The scrolling flags.
			* @return bool True if the window is scrolled successfully, false otherwise.
			*/
			bool ScrollWindowEx(
				int dx,
				int dy,
				LPCRECT lprcScroll = nullptr,
				LPCRECT lprcClip = nullptr,
				HRGN hrgnUpdate = nullptr,
				LPRECT lprcUpdate = nullptr,
				UINT flags = SW_INVALIDATE
			);

			/**
			* @brief Sets the parameters of a scroll bar.
			*
			* @param nBar The type of scroll bar.
			* @param lpsi A pointer to a SCROLLINFO structure that contains the parameters.
			* @param bRedraw Specifies whether the scroll bar should be redrawn to reflect the changes.
			* @return int The previous position of the scroll box.
			*/
			int SetScrollInfo(
				int nBar,
				LPSCROLLINFO lpsi,
				bool bRedraw = true
			);

			/**
			* @brief Sets the position of the scroll box (thumb) in the specified scroll bar.
			*
			* @param nBar The type of scroll bar.
			* @param nPos The new position of the scroll box.
			* @param bRedraw Specifies whether the scroll bar should be redrawn to reflect the changes.
			* @return int The previous position of the scroll box.
			*/
			int SetScrollPos(
				int nBar,
				int nPos,
				bool bRedraw = true
			);

			/**
			* @brief Sets the minimum and maximum scrolling positions for the specified scroll bar.
			*
			* @param nBar The type of scroll bar.
			* @param nMinPos The new minimum scrolling position.
			* @param nMaxPos The new maximum scrolling position.
			* @param bRedraw Specifies whether the scroll bar should be redrawn to reflect the changes.
			* @return bool True if the minimum and maximum positions are set successfully, false otherwise.
			*/
			bool SetScrollRange(
				int nBar,
				int nMinPos,
				int nMaxPos,
				bool bRedraw = true
			);

			/**
			* @brief Sets the page size of the specified scroll bar.
			*
			* @param nBar The type of scroll bar.
			* @param nPage The new page size.
			* @param bRedraw Specifies whether the scroll bar should be redrawn to reflect the changes.
			*/
			void SetScrollPage(
				int nBar,
				UINT nPage,
				bool bRedraw = true
			);

			/**
			* @brief Shows or hides the specified scroll bar.
			*
			* @param wBar The type of scroll bar.
			* @param bShow Specifies whether the scroll bar should be shown or hidden.
			* @return bool True if the scroll bar is shown or hidden successfully, false otherwise.
			*/
			bool ShowScrollBar(
				int wBar,
				bool bShow = true
			);

			/**
			* @brief Gets the auto scroll information for the specified scroll direction.
			*
			* @param bVert Whether the scroll direction is vertical or not.
			* @param nPixelsPerPos The number of pixels per scroll position.
			* @param pfnCaller The caller function.
			* @return bool True if the auto scroll information is retrieved successfully, false otherwise.
			*/
			virtual bool GetAutoScrollInfo(bool bVert, int& nPixelsPerPos, PVOID pfnCaller);

			/**
			* @brief Handles the window messages.
			*
			* @param Msg The window message.
			* @param wParam The WPARAM parameter.
			* @param lParam The LPARAM parameter.
			* @return LRESULT The result of the window procedure.
			*/
			virtual LRESULT WindowProc(UINT Msg, WPARAM wParam, LPARAM lParam);

			/**
			* @brief Handles the WM_PAINT message.
			*
			* @param hdc The handle to the device context.
			*/
			virtual void OnPaint(HDC hdc);

			/**
			* @brief Draws the window with the specified hDC.
			*
			* @param hdc The handle to the device context.
			* @param Rect The rectangle to draw.
			*/
			virtual void OnDraw(HDC hdc, RECT Rect);

			/**
			* @brief Handles the WM_ERASEBKGND.
			*
			* @param hdc The handle to the device context.
			* @return bool True if it erases the background; otherwise, it should return false.
			*/
			virtual bool OnEraseBackground(HDC hdc);

			/**
			* @brief Handles the WM_CLOSE message.
			*/
			virtual void OnClose();

			/**
			* @brief Handles the WM_DESTROY message.
			*/
			virtual void OnDestroy();

			/**
			* @brief Handles the WM_COMMAND message.
			*
			* @param CmdStruct The command structure.
			*/
			virtual void OnCommand(const CommandStruct& CmdStruct);

			/**
			* @brief Handles the WM_NOTIFY message.
			*
			* @param lpNotifyMsgHdr The pointer to the notify message header.
			* @return LRESULT The result of the notify event.
			*/
			virtual LRESULT OnNotify(LPNMHDR lpNotifyMsgHdr);

			/**
			* @brief Handles the WM_WINDOWPOSCHANGING message.
			*
			* @param pWindowPos The pointer to the window position structure.
			*/
			virtual void OnWindowPosChanging(PWINDOWPOS pWindowPos);

			/**
			* @brief Handles the WM_WINDOWPOSCHANGED message.
			*
			* @param pWindowPos The pointer to the window position structure.
			*/
			virtual void OnWindowPosChanged(PWINDOWPOS pWindowPos);

			/**
			* @brief Handles the WM_MOVE message.
			*
			* @param X The new x-coordinate of the window.
			* @param Y The new y-coordinate of the window.
			*/
			virtual void OnMove(int X, int Y);

			/**
			* @brief Handles the WM_MOVING message.
			*
			* @param pRect The pointer to the rectangle that defines the new position of the window.
			*/
			virtual void OnMoving(PRECT pRect);

			/**
			* @brief Handles the WM_SIZE message.
			*
			* @param ResizeType The type of resizing operation that caused the size change.
			* @param nClientWidth The new width of the client area of the window.
			* @param nClientHeight The new height of the client area of the window.
			* @param WindowBatchPositioner The batch positioner for the window.
			*/
			virtual void OnSize(BYTE ResizeType, int nClientWidth, int nClientHeight, WindowBatchPositioner);

			/**
			* @brief Handles the WM_SIZING message.
			*
			* @param Edge The edge of the window that is being sized.
			* @param pRect The pointer to the rectangle that defines the new size of the window.
			*/
			virtual void OnSizing(BYTE Edge, PRECT pRect);

			/**
			* @brief Handles the WM_NCACTIVE message.
			*
			* @param bActive Whether the window is being activated or deactivated.
			* @param hRgn The handle to the region that needs to be redrawn.
			* @return bool True if the default non-client area drawing should be performed, false otherwise.
			*/
			virtual bool OnNcActivate(bool bActive, HRGN hRgn);

			union NcCalcSizeParam
			{
				NCCALCSIZE_PARAMS* pParams;
				LPRECT pRect;
			};

			/**
			* @brief Handles the WM_NCCALCSIZE message.
			*
			* @param bCalcValidRects Whether the application should calculate the valid destination rectangles.
			* @param ncsParam The parameter for the non-client area calculation.
			* @return WORD The combination of flags that indicate which parts of the window frame need to be redrawn.
			*/
			virtual WORD OnNcCalcSize(bool bCalcValidRects, NcCalcSizeParam ncsParam);

			/**
			* @brief Handles the WM_NCDESTROY message.
			*/
			virtual void OnNcDestroy();

			/**
			* @brief Handles the WM_NCPAINT message.
			*
			* @param hRgn The handle to the region to be painted.
			*/
			virtual void OnNcPaint(HRGN hRgn);

			/**
			* @brief Handles the WM_GETMINMAXINFO message.
			*
			* @param pMinMaxInfo A pointer to the MINMAXINFO structure that contains the window's minimum and maximum size information.
			*/
			virtual void OnGetMinMaxInfo(PMINMAXINFO pMinMaxInfo);

			/**
			* @brief Handles the WM_SETTEXT message.
			*
			* @param pText The new window text.
			* @return LRESULT The result of the message processing.
			*/
			virtual LRESULT OnSetText(PCWSTR pText);

			/**
			* @brief Handles the WM_GETTEXT message.
			*
			* @param cchTextMax The maximum number of characters to be copied to the buffer, including the null-terminating character.
			* @param pText The buffer that receives the text.
			* @return int The number of characters copied to the buffer, excluding the null-terminating character.
			*/
			virtual int OnGetText(int cchTextMax, PWSTR pText);

			/**
			* @brief Handles the WM_GETTEXTLENGTH message.
			*
			* @return int The length of the window text, in characters.
			*/
			virtual int OnGetTextLength();

			/**
			* @brief Handles the WM_GETFONT message.
			*
			* @return HFONT The handle to the font used by the window.
			*/
			virtual HFONT OnGetFont();

			/**
			* @brief Handles the WM_SETFONT message.
			*
			* @param hFont The handle to the font to be used by the window.
			* @param bRedraw Specifies whether the window should be redrawn immediately after setting the font.
			* @return HFONT The handle to the previous font used by the window.
			*/
			virtual HFONT OnSetFont(HFONT hFont, bool bRedraw);

			/**
			* @brief Handles the WM_SETTINGCHANGE message.
			*
			* @param uiAction The system-wide setting that has changed.
			* @param pConfiguration The name of the updated section of the user profile or the system policy.
			*/
			virtual void OnSettingChanged(UINT uiAction, PCWSTR pConfiguration);

			/**
			* @brief Handles the WM_STYLECHANGED message.
			*
			* @param nLongIndex The index of the style value that has changed.
			* @param lpStyleStruct A pointer to a STYLESTRUCT structure that contains the new style information.
			*/
			virtual void OnStyleChanged(int nLongIndex, const STYLESTRUCT* lpStyleStruct);

			/**
			* @brief Handles the WM_STYLECHANGING message.
			*
			* @param nLongIndex The index of the style value that is changing.
			* @param lpStyleStruct A pointer to a STYLESTRUCT structure that contains the new style information.
			*/
			virtual void OnStyleChanging(int nLongIndex, STYLESTRUCT* lpStyleStruct);

			/**
			* @brief Handles the WM_DEVICECHANGE message.
			*
			* @param wDeviceEvent The type of device event.
			* @param lpEventData A pointer to the device-specific information.
			* @return DWORD The result of the message processing.
			*/
			virtual DWORD OnDeviceChanged(WORD wDeviceEvent, LPCVOID lpEventData);

			/**
			* @brief Handles the WM_THEMECHANGED message.
			*/
			virtual void OnThemeChanged();

			/**
			* @brief Handles the WM_SHOWWINDOW message.
			*
			* @param bIsBeingShown Specifies whether the window is being shown or hidden.
			* @param Status Specifies the reason for the show or hide operation.
			*/
			virtual void OnShowWindow(bool bIsBeingShown, int Status);

			/**
			* @brief Handles the WM_ENABLE message.
			*
			* @param bIsEnabled Specifies whether the window is being enabled or disabled.
			*/
			virtual void OnEnable(bool bIsEnabled);

			/**
			* @brief Handles the WM_HSCROLL message.
			*
			* @param nScrollCode Specifies the scroll code.
			* @param nPos Specifies the scroll position.
			* @param pScrollBar Pointer to the ScrollBar control. Default is nullptr.
			*/
			virtual void OnHorzScroll(int nScrollCode, int nPos, ScrollBar* pScrollBar = nullptr);

			/**
			* @brief Handles the WM_VSCROLL message.
			*
			* @param nScrollCode Specifies the scroll code.
			* @param nPos Specifies the scroll position.
			* @param pScrollBar Pointer to the ScrollBar control. Default is nullptr.
			*/
			virtual void OnVertScroll(int nScrollCode, int nPos, ScrollBar* pScrollBar = nullptr);

			/**
			* @brief Handles the WM_GESTURE message.
			*
			* @param ullArguments Specifies the gesture arguments.
			* @param hGestureInfo Handle to the gesture information structure.
			*/
			virtual void OnGesture(ULONGLONG ullArguments, HGESTUREINFO hGestureInfo);

			/**
			* @brief Handles the WM_TOUCH message.
			*
			* @param cInputs Specifies the number of touch inputs.
			* @param hTouchInput Handle to the touch input information.
			*/
			virtual void OnTouch(UINT cInputs, HTOUCHINPUT hTouchInput);

			/**
			* @brief Handles the WM_MOUSEWHEEL message.
			*
			* @param nDelta Specifies the distance the wheel is rotated.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			*/
			virtual void OnMouseWheel(short nDelta, UINT uKeys, int X, int Y);

			/**
			* @brief Handles the WM_MOUSEHWHEEL message.
			*
			* @param nDelta Specifies the distance the wheel is rotated.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			*/
			virtual void OnMouseHWheel(short nDelta, UINT uKeys, int X, int Y);

			/**
			* @brief Handles the WM_MOUSEHOVER message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnMouseHover(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_MOUSELEAVE message.
			*/
			virtual void OnMouseLeave();

			/**
			* @brief Handles the WM_MOUSEMOVE message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param nKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnMouseMove(int X, int Y, UINT nKeys);

			/**
			* @brief Handles the WM_LBUTTONDOWN message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnLButtonDown(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_LBUTTONUP message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param nKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnLButtonUp(int X, int Y, UINT nKeys);

			/**
			* @brief Handles the WM_LBUTTONDBLCLK message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnLButtonDblClk(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_RBUTTONDOWN message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnRButtonDown(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_RBUTTONUP message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnRButtonUp(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_RBUTTONDBLCLK message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnRButtonDblClk(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_MBUTTONDOWN message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnMButtonDown(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_MBUTTONUP message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnMButtonUp(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_MBUTTONDBLCLK message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnMButtonDblClk(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_XBUTTONDOWN message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnXButtonDown(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_XBUTTONUP message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnXButtonUp(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_XBUTTONDBLCLK message.
			*
			* @param X Specifies the x-coordinate of the mouse pointer.
			* @param Y Specifies the y-coordinate of the mouse pointer.
			* @param uKeys Specifies the state of the modifier keys and mouse buttons.
			*/
			virtual void OnXButtonDblClk(int X, int Y, UINT uKeys);

			/**
			* @brief Handles the WM_KEYDOWN message.
			*
			* @param uVirtKey Specifies the virtual-key code of the key.
			* @param cRepeat Specifies the repeat count for the current message.
			* @param ScanCode Specifies the scan code.
			* @param bExtended Specifies whether the key is an extended key.
			* @param bAltDown Specifies whether the ALT key is down.
			*/
			virtual void OnKeyDown(UINT uVirtKey, WORD cRepeat, BYTE ScanCode, bool bExtended, bool bAltDown);

			/**
			* @brief Handles the WM_KEYUP message.
			*
			* @param uVirtKey Specifies the virtual-key code of the key.
			* @param cRepeat Specifies the repeat count for the current message.
			* @param ScanCode Specifies the scan code.
			* @param bExtended Specifies whether the key is an extended key.
			* @param bAltDown Specifies whether the ALT key is down.
			*/
			virtual void OnKeyUp(UINT uVirtKey, WORD cRepeat, BYTE ScanCode, bool bExtended, bool bAltDown);

			/**
			* @brief Handles the WM_APPCOMMAND message.
			*
			* @param Window The window where the user clicked the button or pressed the key. This can be a child window of the window receiving the message.
			* @param nCmd The command identifier.
			* @param uDevice The device identifier.
			* @param dwKeys The key state.
			*/
			virtual void OnAppCommand(WindowBase Window, int nCmd, UINT uDevice, DWORD dwKeys);

			/**
			* @brief Handles the WM_CHAR message.
			*
			* @param chChar Specifies the character code of the key.
			* @param cRepeat Specifies the repeat count for the current message.
			* @param ScanCode Specifies the scan code.
			* @param bExtended Specifies whether the key is an extended key.
			* @param bAltDown Specifies whether the ALT key is down.
			*/
			virtual void OnChar(WCHAR chChar, WORD cRepeat, BYTE ScanCode, bool bExtended, bool bAltDown);

			/**
			* @brief Handles the WM_TIMER message.
			*
			* @param nIDEvent Specifies the identifier of the timer.
			* @param lpTimerProc Specifies the address of the timer procedure.
			*/
			virtual void OnTimer(UINT_PTR nIDEvent, TIMERPROC lpTimerProc);

			/**
			* @brief Handles the WM_COPYDATA message.
			*
			* @param Window Specifies the window passing the data.
			* @param pCopyDataStruct A pointer to a COPYDATASTRUCT structure that contains the data to be passed to the window.
			* @return bool If the receiving application processes this message, it should return true; otherwise, it should return false.
			*/
			virtual bool OnCopyData(WindowBase Window, PCOPYDATASTRUCT pCopyDataStruct);

			/**
			* @brief Handles the WM_CTLCOLORBTN message.
			*
			* @param hDC Specifies the device context.
			* @param Window Specifies the window that sent the message.
			* @return HBRUSH The handle to the brush that the system uses to paint the control.
			*/
			virtual HBRUSH OnControlColorButton(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLOREDIT message.
			*
			* @param hDC Specifies the device context.
			* @param Window Specifies the window that sent the message.
			* @return HBRUSH The handle to the brush that the system uses to paint the control.
			*/
			virtual HBRUSH OnControlColorEdit(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLORLISTBOX message.
			*
			* @param hDC Specifies the device context.
			* @param Window Specifies the window that sent the message.
			* @return HBRUSH The handle to the brush that the system uses to paint the control.
			*/
			virtual HBRUSH OnControlColorListBox(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLORSCROLLBAR message.
			*
			* @param hDC Specifies the device context.
			* @param Window Specifies the window that sent the message.
			* @return HBRUSH The handle to the brush that the system uses to paint the control.
			*/
			virtual HBRUSH OnControlColorScrollBar(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLORSTATIC message.
			*
			* @param hDC Specifies the device context.
			* @param Window Specifies the window that sent the message.
			* @return HBRUSH The handle to the brush that the system uses to paint the control.
			*/
			virtual HBRUSH OnControlColorStatic(HDC hDC, WindowBase Window);
		};


		/**
		* @brief Checks if dark mode is enabled.
		*
		* @return bool True if dark mode is enabled, false otherwise.
		*/
		bool LOURDLE_UIFRAMEWORK_API IsDarkModeEnabled();

		/**
		* @brief Checks if the application can use dark mode.
		*
		* @return bool True if the application can use dark mode, false otherwise.
		*/
		bool LOURDLE_UIFRAMEWORK_API CanAppUseDarkMode();



		class SysControl;

		/**
		* @brief Represents a dialog window in the UI framework.
		*/
		class LOURDLE_UIFRAMEWORK_API Dialog : public WindowBase
		{
			friend class SysControl;
		public:
			/**
			* @brief Default constructor. Deleted to prevent usage.
			*/
			Dialog() = delete;

			/**
			* @brief Move constructor. Deleted to prevent usage.
			*/
			Dialog(const Dialog&&) = delete;

			/**
			* @brief Move constructor. Deleted to prevent usage.
			*/
			Dialog(Dialog&&) = delete;

			/**
			* @brief Constructs a dialog window with the specified parameters.
			*
			* @param Parent The parent window of the dialog.
			* @param nWidth The width of the dialog.
			* @param nHeight The height of the dialog.
			* @param dwStyle The window style of the dialog.
			* @param pDialogTitle The title of the dialog.
			* @param bAllowDarkMode Whether to allow dark mode for the dialog. Default is true.
			*/
			Dialog(WindowBase* Parent, short nWidth, short nHeight, DWORD dwStyle, PCWSTR pDialogTitle, bool bAllowDarkMode = true);

			/**
			* @brief Destructor.
			*/
			virtual ~Dialog();

			/**
			* @brief Initializes the dialog window.
			*
			* @param lParam The initialization parameter.
			*/
			virtual void Init(LPARAM) = 0;

			/**
			* @brief Handles the WM_GETDLGCODE message, allowing a control to customize
			*        its behavior related to keyboard input and navigation.
			*
			* @param uVirtKey The virtual key code of the key being processed (if applicable).
			* @param pMsg A pointer to the MSG structure containing additional message information, or nullptr if not available.
			*
			* @return WORD A code indicating the control's desired behavior. Common return
			*              values include DLGC_WANTALLKEYS, DLGC_WANTARROWS, or other DLGC_*
			*              flags to specify how the control handles input or navigation.
			*/
			virtual WORD GetDlgCode(UINT uVirtKey, PMSG pMsg);

			/**
			* @brief Handles the WM_NEXTDLGCTL message to manage focus navigation within a dialog box.
			*
			* This method updates the keyboard focus to a specific control or moves the focus
			* to the next/previous control in the tab order, depending on the parameters.
			*
			* @param bNext Indicates whether to move focus to the next or previous control.
			*              If true, focus moves to the next control; if false, focus moves to the previous control.
			* @param hCtrl A handle to the control that should receive focus, or nullptr if
			*              the focus should follow sequential tab navigation.
			*/
			virtual void OnNextDlgCtl(bool bNext, HWND hCtrl);

			/**
			* @brief Structure representing a command.
			*/
			struct CommandStruct
			{
				WORD wID; /**< The ID of the control. */
				WORD wCode; /**< The code of the command. */
			};

			/**
			* @brief Registers a command with a command procedure.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			* @param lParam The command parameter.
			* @param Object The object associated with the command.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(PVOID Object, LPARAM lParam), LPARAM lParam, PVOID Object);

			/**
			* @brief Registers a command with a command procedure.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			* @param lParam The command parameter.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(Dialog*, LPARAM), LPARAM lParam);

			/**
			* @brief Registers a command with a command procedure.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(Dialog*));

			/**
			* @brief Registers a command with a command procedure.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			* @param lParam The command parameter.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(HWND, LPARAM), LPARAM lParam);

			/**
			* @brief Registers a command with a command procedure.
			*
			* @param CmdStruct The command structure.
			* @param CmdProc The command procedure.
			*/
			void RegisterCommand(const CommandStruct& CmdStruct, void(*CmdProc)(HWND));

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam C The type of the object.
			* @tparam T The type of the return value of the command procedure.
			* @tparam U The type of the argument passed to the command procedure.
			* @param CmdProc The command procedure.
			* @param Obj The pointer to the object associated with the command procedure.
			* @param Arg The argument passed to the command procedure.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<class C, class CArg, typename T, typename U, typename UArg>
			::std::enable_if_t<sizeof(U) == sizeof(LPARAM)>
				RegisterCommand(T(C::* CmdProc)(U), CArg*&& Obj, UArg&& Arg, WORD wID, WORD wNotifyCode)
			{
				U convArg = ::std::forward<UArg>(Arg);
				RegisterCommand({ wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), *reinterpret_cast<LPARAM*>(&convArg),
					::std::forward<C*>(Obj));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam C The type of the object.
			* @tparam T The type of the return value of the command procedure.
			* @param CmdProc The command procedure.
			* @param Obj The pointer to the object associated with the command procedure.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<class C, class CArg, typename T>
			void RegisterCommand(T(C::* CmdProc)(), CArg*&& Obj, WORD wID, WORD wNotifyCode)
			{
				RegisterCommand({ wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0,
					::std::forward<CArg*>(Obj));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam T The type of the return value of the command procedure.
			* @param CmdProc The command procedure.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<typename T>
			void RegisterCommand(T(*CmdProc)(), WORD wID, WORD wNotifyCode)
			{
				RegisterCommand({ wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0, nullptr);
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam T The type of the return value of the command procedure.
			* @tparam U The type of the argument passed to the command procedure.
			* @param CmdProc The command procedure.
			* @param Arg The argument passed to the command procedure.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<typename T, typename U, typename UArg>
			::std::enable_if_t<sizeof(U) == sizeof(PVOID)>
				RegisterCommand(T(*CmdProc)(U), UArg&& Arg, WORD wID, WORD wNotifyCode)
			{
				U convArg = ::std::forward<UArg>(Arg);
				RegisterCommand({ wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0,
					*reinterpret_cast<PVOID>(&convArg));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam T The type of the return value of the command procedure.
			* @tparam U The type of the 1st argument passed to the command procedure.
			* @tparam V The type of the 2nd argument passed to the command procedure.
			* @param CmdProc The command procedure.
			* @param Arg The argument passed to the command procedure.
			* @param wID The ID of the control.
			* @param wNotifyCode The code of the command.
			*/
			template<typename T, typename U, typename V, typename UArg, typename VArg>
			::std::enable_if_t<sizeof(U) == sizeof(PVOID) && sizeof(V) == sizeof(LPARAM)>
				RegisterCommand(T(*CmdProc)(U, V), UArg&& Arg1, VArg&& Arg2, WORD wID, WORD wNotifyCode)
			{
				U convArg1 = ::std::forward<UArg>(Arg1);
				V convArg2 = ::std::forward<VArg>(Arg2);
				RegisterCommand({ wID, wNotifyCode },
					*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc),
					*reinterpret_cast<LPARAM*>(&convArg2),
					*reinterpret_cast<PVOID*>(&convArg1));
			}

			/**
			* @brief Registers a command with a command procedure.
			*
			* @tparam Dlg The type of the dialog.
			* @param CmdProc The command procedure.
			* @param wID The ID of the control.
			* @param wNotifyCode The notify code of the command.
			*/
			template<class Dlg>
			::std::enable_if_t<::std::is_base_of<Dialog, Dlg>::value>
				RegisterCommand(void(Dlg::* CmdProc)(), WORD wID, WORD wNotifyCode)
			{
				auto p = dynamic_cast<Dlg*>(this);
				if (p)
					RegisterCommand({ wID, wNotifyCode },
						*reinterpret_cast<void(**)(PVOID, LPARAM)>(&CmdProc), 0, p);
			}

			/**
			* @brief Unregisters a command.
			*
			* @param CmdStruct The command structure.
			*/
			void UnregisterCommand(const CommandStruct& CmdStruct);

			/**
			* @brief Unregisters a command.
			*
			* @param hWnd The handle to the window.
			*/
			void UnregisterCommand(HWND hWnd);

			/**
			* @brief Gets a pointer to the object associated with the dialog window.
			*
			* @param hDlg The handle to the dialog window.
			* @return Dialog* A pointer to the object associated with the dialog window.
			*/
			static Dialog* GetObjectPointer(HWND hDlg);

			/**
			* @brief Gets a pointer to the object associated with the dialog window.
			*
			* @tparam Dlg The type of the dialog.
			* @param hDlg The handle to the dialog window.
			* @return Dlg* A pointer to the object associated with the dialog window.
			*/
			template<class Dlg>
			static Dlg* GetObjectPointer(HWND hDlg)
			{
				static_assert(::std::is_base_of<Dialog, Dlg>::value, "Invalid template parameter. The template parameter must be a class derived from Dialog.");
				return dynamic_cast<Dlg*>(GetObjectPointer(hDlg));
			}

			/**
			* @brief Displays a message box with the specified text, caption, and type.
			*
			* @param lpText The text of the message box.
			* @param lpCaption The caption of the message box.
			* @param uType The type of the message box.
			* @return int The result of the message box.
			*/
			int MessageBox(LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);

#ifdef SHFOLDERAPI
			/**
			* @brief Displays a folder browser dialog.
			*
			* @param lpbi The browse information.
			* @return LPITEMIDLIST The selected folder.
			*/
			LPITEMIDLIST SHBrowseForFolder(LPBROWSEINFOW lpbi);
#endif

			/**
			* @brief Displays an error message box.
			*
			* @param uType The type of the message box. Default is MB_ICONERROR.
			* @return int The result of the message box.
			*/
			int ErrorMessageBox(UINT uType = MB_ICONERROR);

			/**
			* @brief Handles the dialog window procedure.
			*
			* @param Msg The message.
			* @param wParam The wParam parameter.
			* @param lParam The lParam parameter.
			* @return INT_PTR The result of the dialog window procedure.
			*/
			virtual INT_PTR DialogProc(UINT Msg, WPARAM wParam, LPARAM lParam);

			/**
			* @brief Handles the WM_PAINT message.
			*/
			virtual void OnPaint();

			/**
			* @brief Draws the dialog window with the specified hDC.
			*
			* @param hDC The handle to the device context.
			* @param Rect The rectangle to draw.
			*/
			virtual void OnDraw(HDC hDC, RECT Rect);

			/**
			* @brief Handles the WM_CLOSE message.
			*
			* @return bool True if you need to prevent the dialog box from closing, and false otherwise.
			*/
			virtual bool OnClose();

			/**
			* @brief Called when the OK (Enter) button is clicked.
			*/
			virtual void OnOK();

			/**
			* @brief Called when the Cancel (Esc) button is clicked.
			*/
			virtual void OnCancel();

			/**
			* @brief Handles the WM_DESTORY message.
			*/
			virtual void OnDestroy();


			/**
			* @brief Handles the WM_COMMAND message.
			*
			* @param CmsStruct The command structure.
			*/
			virtual void OnCommand(const CommandStruct& CmsStruct);

			/**
			* @brief Handles the WM_NOTIFY message.
			*
			* @param lpNotifyMsgHdr The pointer to the notify message header.
			* @return LRESULT The result of the notify message.
			*/
			virtual LRESULT OnNotify(LPNMHDR lpNotifyMsgHdr);

			/**
			* @brief Handles the WM_WINDOWPOSCHANGING message.
			*
			* @param pWindowPos The pointer to the window position.
			*/
			virtual void OnWindowPosChanging(PWINDOWPOS pWindowPos);

			/**
			* @brief Handles the WM_WINDOWPOSCHANGED message.
			*
			* @param pWindowPos The pointer to the window position.
			*/
			virtual void OnWindowPosChanged(PWINDOWPOS pWindowPos);

			/**
			* @brief Handles the WM_MOVE message.
			*
			* @param X The x-coordinate of the window.
			* @param Y The y-coordinate of the window.
			*/
			virtual void OnMove(int X, int Y);

			/**
			* @brief Handles the WM_MOVING message.
			*
			* @param pRect The pointer to the rectangle.
			*/
			virtual void OnMoving(PRECT pRect);

			/**
			* @brief Handles the WM_SIZE message.
			*
			* @param ResizeType The type of the resize.
			* @param nClientWidth The width of the client area.
			* @param nClientHeight The height of the client area.
			* @param WindowBatchPositioner The window batch positioner.
			*/
			virtual void OnSize(BYTE ResizeType, int nClientWidth, int nClientHeight, WindowBatchPositioner);

			/**
			* @brief Handles the WM_SIZING message.
			*
			* @param Edge The edge of the sizing.
			* @param pRect The pointer to the rectangle.
			*/
			virtual void OnSizing(BYTE Edge, PRECT pRect);

			/**
			* @brief Handles the WM_GETMINMAXINFO message.
			*
			* @param pMinMaxInfo The pointer to the minmax information.
			*/
			virtual void OnGetMinMaxInfo(PMINMAXINFO pMinMaxInfo);

			/**
			* @brief Handles the WM_CTLCOLORBTN message.
			*
			* @param hDC The handle to the device context.
			* @param Window The window.
			* @return HBRUSH The handle to the brush.
			*/
			virtual HBRUSH OnControlColorButton(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLOREDIT message.
			*
			* @param hDC The handle to the device context.
			* @param Window The window.
			* @return HBRUSH The handle to the brush.
			*/
			virtual HBRUSH OnControlColorEdit(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLORLISTBOX message.
			*
			* @param hDC The handle to the device context.
			* @param Window The window.
			* @return HBRUSH The handle to the brush.
			*/
			virtual HBRUSH OnControlColorListBox(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLORSTATIC message.
			*
			* @param hDC The handle to the device context.
			* @param Window The window.
			* @return HBRUSH The handle to the brush.
			*/
			virtual HBRUSH OnControlColorStatic(HDC hDC, WindowBase Window);

			/**
			* @brief Handles the WM_CTLCOLORDLG message.
			*
			* @param hDC The handle to the device context.
			* @param Window The window.
			* @return HBRUSH The handle to the brush.
			*/
			virtual HBRUSH OnControlColorDialog(HDC hDC, WindowBase Window);

			/**
			* @brief Displays the dialog window as a modal dialog box.
			*
			* @param lParam The initialization parameter.
			* @return INT_PTR The result of the modal dialog box.
			*/
			INT_PTR ModalDialogBox(LPARAM lParam);

			/**
			* @brief Creates the dialog window as a modeless dialog.
			*
			* @param lParam The initialization parameter.
			* @return bool True if the modeless dialog is created successfully, false otherwise.
			*/
			bool CreateModelessDialog(LPARAM lParam);

			/**
			* @brief Ends the modal dialog box.
			*
			* Destroys a modal dialog box, causing the system to end any processing for the dialog box.
			* For modeless dialog, use DestroyWindow instead.
			* 
			* @param nResult The result of the modal dialog box.
			* @return bool True if the modal dialog box is ended successfully, false otherwise.
			*/
			bool EndDialog(INT_PTR nResult);

			/**
			* @brief Checks if the dialog is in a modal state.
			*
			* @return bool True if the dialog is in a modal state, false otherwise.
			*/
			bool IsModalState();


			String DialogTitle; /**< The title of the dialog. */
			HFONT hFont; /**< The font of the dialog. */

			UINT bAllowDarkMode : 1; /**< Whether dark mode is allowed for the dialog. */

			short cx; /**< The width of the dialog. */
			short cy; /**< The height of the dialog. */
			DWORD dwStyle; /**< The window style of the dialog. */
		protected:
			WindowBase* Parent; /**< The parent window of the dialog. */
		private:
			PVOID pInitializationRules; /**< The initialization rules of the dialog. */
			PVOID pCommandMap; /**< The command map of the dialog. */
		};


		/**
		* @brief A custom dialog class that extends the base Dialog class.
		* 
		* @tparam Wnd The type of the parent window.
		*/
		template<class Wnd = WindowBase>
		class DialogEx : public Dialog
		{
		public:
			/**
			* @brief Deleted default constructor.
			*/
			DialogEx() = delete;

			/**
			* @brief Deleted move constructor.
			*/
			DialogEx(const Dialog&&) = delete;

			/**
			* @brief Deleted copy constructor.
			*/
			DialogEx(Dialog&) = delete;

			/**
			* @brief Constructs a DialogEx object with the specified parameters.
			*
			* @param Parent The parent window.
			* @param nWidth The width of the dialog.
			* @param nHeight The height of the dialog.
			* @param dwStyle The window style.
			* @param pDialogTitle The title of the dialog.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			DialogEx(Wnd* Parent, short nWidth, short nHeight, DWORD dwStyle, PCWSTR pDialogTitle, bool bAllowDarkMode = true) :
				Dialog(Parent, nWidth, nHeight, dwStyle, pDialogTitle, bAllowDarkMode)
			{
			}

			/**
			* @brief Default destructor.
			*/
			virtual ~DialogEx() = default;

			/**
			* @brief Initializes the dialog.
			*
			* @param lParam The initialization parameter.
			*/
			virtual void Init(LPARAM lParam) {}

			/**
			* @brief Gets the parent window.
			*
			* @return Wnd* A pointer to the parent window.
			*/
			Wnd* GetParent()
			{
				return static_cast<Wnd*>(Parent);
			}
		};

		/**
		* @brief A custom dialog class that extends the DialogEx class and supports passing a single custom type parameter to the initialization method.
		* 
		* @tparam Wnd The type of the parent window.
		* @tparam T The type of the initialization parameter.
		* @tparam Enable A type used for template specialization. Must be void.
		*/
		template<class Wnd = WindowBase, typename T = void, typename Enable = void>
		class DialogEx2 : public DialogEx<Wnd>
		{
			static_assert(::std::is_same_v<Enable, void>, "Invalid template parameter. The 3rd template parameter must be void.");

			void Init(LPARAM lParam) override final
			{
				void(DialogEx2:: * pfnInit)(T) = &DialogEx2::Init;
				(this->*pfnInit)(static_cast<T>(*reinterpret_cast<::std::remove_reference_t<T>*>(lParam)));
			}

		public:
			/**
			* @brief Deleted default constructor.
			*/
			DialogEx2() = delete;

			/**
			* @brief Deleted move constructor.
			*/
			DialogEx2(const Dialog&&) = delete;

			/**
			* @brief Deleted copy constructor.
			*/
			DialogEx2(Dialog&&) = delete;

			/**
			* @brief Constructs a DialogEx2 object with the specified parameters.
			*
			* @param Parent The parent window.
			* @param nWidth The width of the dialog.
			* @param nHeight The height of the dialog.
			* @param dwStyle The window style.
			* @param pDialogTitle The title of the dialog.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			DialogEx2(Wnd* Parent, short nWidth, short nHeight, DWORD dwStyle, PCWSTR pDialogTitle, bool bAllowDarkMode = true) :
				DialogEx<Wnd>(Parent, nWidth, nHeight, dwStyle, pDialogTitle, bAllowDarkMode)
			{
			}

			/**
			* @brief Default destructor.
			*/
			virtual ~DialogEx2() = default;

			/**
			* @brief Initializes the dialog with the specified initialization parameter.
			*
			* @param Param The initialization parameter.
			*/
			virtual void Init(T Param) = 0;

			/**
			* @brief Displays the dialog as a modal dialog box.
			*
			* @param Param The initialization parameter.
			* @return INT_PTR The result of the dialog box.
			*/
			INT_PTR ModalDialogBox(T Param)
			{
				return Dialog::ModalDialogBox(reinterpret_cast<LPARAM>(&Param));
			}

			/**
			* @brief Creates the dialog as a modeless dialog.
			*
			* @param Param The initialization parameter.
			* @return bool True if the dialog is created successfully, false otherwise.
			*/
			bool CreateModelessDialog(T Param)
			{
				return Dialog::CreateModelessDialog(reinterpret_cast<LPARAM>(&Param));
			}
		};

		template<class Wnd, typename T>
		class DialogEx2<Wnd, T, ::std::enable_if_t<!::std::is_reference_v<T> && !::std::is_same_v<T, LPARAM>>>
			: public DialogEx<Wnd>
		{

			void Init(LPARAM lParam) override final
			{
				Init(::std::move(*reinterpret_cast<T*>(lParam)));
			}

		public:
			DialogEx2() = delete;
			DialogEx2(const Dialog&&) = delete;
			DialogEx2(Dialog&&) = delete;

			/**
			* @brief Constructs a DialogEx2 object with the specified parameters.
			*
			* @param Parent The parent window of the dialog.
			* @param nWidth The width of the dialog.
			* @param nHeight The height of the dialog.
			* @param dwStyle The style of the dialog.
			* @param pDialogTitle The title of the dialog.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			DialogEx2(Wnd* Parent, short nWidth, short nHeight, DWORD dwStyle, PCWSTR pDialogTitle, bool bAllowDarkMode = true) :
				DialogEx<Wnd>(Parent, nWidth, nHeight, dwStyle, pDialogTitle, bAllowDarkMode)
			{
			}

			virtual ~DialogEx2() = default;

			/**
			* @brief Initializes the dialog with the specified parameter.
			*
			* @param Param The parameter to initialize the dialog with.
			*/
			virtual void Init(T Param) = 0;

			/**
			* @brief Displays the dialog as a modal dialog box.
			*
			* @param Param The parameter to pass to the dialog.
			* @return INT_PTR The result of the dialog box.
			*/
			INT_PTR ModalDialogBox(T Param)
			{
				return Dialog::ModalDialogBox(reinterpret_cast<LPARAM>(&Param));
			}

			/**
			* @brief Creates the dialog as a modeless dialog.
			*
			* @param Param The parameter to pass to the dialog.
			* @return bool True if the dialog was created successfully, false otherwise.
			*/
			bool CreateModelessDialog(T Param)
			{
				return Dialog::CreateModelessDialog(reinterpret_cast<LPARAM>(&Param));
			}
		};

		template<class Wnd, typename T>
		class DialogEx2<Wnd, T, ::std::enable_if_t<::std::is_same_v<T, LPARAM>>>
			: public DialogEx<Wnd>
		{
		public:
			DialogEx2() = delete;
			DialogEx2(const Dialog&&) = delete;
			DialogEx2(Dialog&&) = delete;

			/**
			* @brief Constructs a DialogEx2 object with the specified parameters.
			*
			* @param Parent The parent window of the dialog.
			* @param nWidth The width of the dialog.
			* @param nHeight The height of the dialog.
			* @param dwStyle The style of the dialog.
			* @param pDialogTitle The title of the dialog.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			DialogEx2(Wnd* Parent, short nWidth, short nHeight, DWORD dwStyle, PCWSTR pDialogTitle, bool bAllowDarkMode = true) :
				DialogEx<Wnd>(Parent, nWidth, nHeight, dwStyle, pDialogTitle, bAllowDarkMode)
			{
			}

			virtual ~DialogEx2() = default;

			/**
			* @brief Initializes the dialog with the specified parameter.
			*
			* @param Param The parameter to initialize the dialog with.
			*/
			virtual void Init(T Param) = 0;

			/**
			* @brief Displays the dialog as a modal dialog box.
			*
			* @param Param The parameter to pass to the dialog.
			* @return INT_PTR The result of the dialog box.
			*/
			INT_PTR ModalDialogBox(T Param)
			{
				return Dialog::ModalDialogBox(Param);
			}

			/**
			* @brief Creates the dialog as a modeless dialog.
			*
			* @param Param The parameter to pass to the dialog.
			* @return bool True if the dialog was created successfully, false otherwise.
			*/
			bool CreateModelessDialog(T Param)
			{
				return Dialog::CreateModelessDialog(Param);
			}
		};

		template<class Wnd>
		class DialogEx2<Wnd, void>
			: public DialogEx<Wnd>
		{
			void Init(LPARAM) override final
			{
				Init();
			}

		public:
			DialogEx2() = delete;
			DialogEx2(const Dialog&&) = delete;
			DialogEx2(Dialog&&) = delete;

			/**
			* @brief Constructs a DialogEx2 object.
			*
			* @param Parent The parent window.
			* @param nWidth The width of the dialog.
			* @param nHeight The height of the dialog.
			* @param dwStyle The window style.
			* @param pDialogTitle The title of the dialog.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			DialogEx2(Wnd* Parent, short nWidth, short nHeight, DWORD dwStyle, PCWSTR pDialogTitle, bool bAllowDarkMode = true) :
				DialogEx<Wnd>(Parent, nWidth, nHeight, dwStyle, pDialogTitle, bAllowDarkMode)
			{
			}
			virtual ~DialogEx2() = default;

			/**
			* @brief Initializes the dialog.
			*/
			virtual void Init() {}

			/**
			* @brief Displays the dialog as a modal dialog box.
			*
			* @return INT_PTR The result of the dialog box.
			*/
			INT_PTR ModalDialogBox()
			{
				return Dialog::ModalDialogBox(0);
			}

			/**
			* @brief Creates the dialog as a modeless dialog box.
			*
			* @return bool True if the dialog is created successfully, false otherwise.
			*/
			bool CreateModelessDialog()
			{
				return Dialog::CreateModelessDialog(0);
			}
		};


		/**
		* @brief A custom dialog class that extends the DialogEx class and supports passing multiple custom type parameters to the initialization method.
		*
		* @tparam Wnd The type of the parent window.
		* @tparam _Args The parameter pack type.
		*/
		template<class Wnd, typename... _Args>
		class DialogEx3 : public DialogEx<Wnd>
		{
			static_assert(sizeof...(_Args) > 1, "Invalid template parameter. The 2nd template parameter must be a parameter pack. If you want to use a single parameter, use DialogEx2 instead.");

			using ArgPack_t = ::std::tuple<_Args&&...>;

			template<size_t... Is>
			void CallInit(ArgPack_t& args, ::std::index_sequence<Is...>)
			{
				Init(static_cast<_Args&&>(::std::get<Is>(args))...);
			}

			void Init(LPARAM lParam) override final
			{
				CallInit(*reinterpret_cast<ArgPack_t*>(lParam), ::std::index_sequence_for<_Args&&...>{});
			}

		public:
			DialogEx3() = delete;
			DialogEx3(const Dialog&&) = delete;
			DialogEx3(Dialog&&) = delete;

			/**
			* @brief Constructs a DialogEx3 object.
			*
			* @param Parent The parent window.
			* @param nWidth The width of the dialog.
			* @param nHeight The height of the dialog.
			* @param dwStyle The window style.
			* @param pDialogTitle The title of the dialog.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			DialogEx3(Wnd* Parent, short nWidth, short nHeight, DWORD dwStyle, PCWSTR pDialogTitle, bool bAllowDarkMode = true) :
				DialogEx<Wnd>(Parent, nWidth, nHeight, dwStyle, pDialogTitle, bAllowDarkMode)
			{
			}

			virtual ~DialogEx3() = default;

			/**
			* @brief Initializes the dialog.
			*
			* @param Args The arguments for initialization.
			*/
			virtual void Init(_Args...) = 0;

			/**
			* @brief Displays the dialog as a modal dialog box.
			*
			* @param Args The arguments for initialization.
			* @return INT_PTR The result of the dialog box.
			*/
			INT_PTR ModalDialogBox(_Args... Args)
			{
				ArgPack_t args = ::std::forward_as_tuple(static_cast<_Args&&>(Args)...);
				return Dialog::ModalDialogBox(reinterpret_cast<LPARAM>(&args));
			}

			/**
			* @brief Creates the dialog as a modeless dialog box.
			*
			* @param Args The arguments for initialization.
			* @return bool True if the dialog is created successfully, false otherwise.
			*/
			bool CreateModelessDialog(_Args... Args)
			{
				ArgPack_t args = ::std::forward_as_tuple(static_cast<_Args&&>(Args)...);
				return Dialog::CreateModelessDialog(reinterpret_cast<LPARAM>(&args));
			}
		};


		/**
		* @brief Overloads the equality operator for comparing two Window::CommandStruct objects.
		*
		* @param lhs The left-hand side CommandStruct object.
		* @param rhs The right-hand side CommandStruct object.
		* @return bool True if the CommandStruct objects are equal, false otherwise.
		*/
		bool LOURDLE_UIFRAMEWORK_API operator==(const Window::CommandStruct& lhs, const Window::CommandStruct& rhs);

		/**
		* @brief Overloads the inequality operator for comparing two Window::CommandStruct objects.
		*
		* @param lhs The left-hand side CommandStruct object.
		* @param rhs The right-hand side CommandStruct object.
		* @return bool True if the CommandStruct objects are not equal, false otherwise.
		*/
		bool LOURDLE_UIFRAMEWORK_API operator!=(const Window::CommandStruct& lhs, const Window::CommandStruct& rhs);

		/**
		* @brief Overloads the equality operator for comparing two Dialog::CommandStruct objects.
		*
		* @param lhs The left-hand side CommandStruct object.
		* @param rhs The right-hand side CommandStruct object.
		* @return bool True if the CommandStruct objects are equal, false otherwise.
		*/
		bool LOURDLE_UIFRAMEWORK_API operator==(const Dialog::CommandStruct& lhs, const Dialog::CommandStruct& rhs);

		/**
		* @brief Overloads the inequality operator for comparing two Dialog::CommandStruct objects.
		*
		* @param lhs The left-hand side CommandStruct object.
		* @param rhs The right-hand side CommandStruct object.
		* @return bool True if the CommandStruct objects are not equal, false otherwise.
		*/
		bool LOURDLE_UIFRAMEWORK_API operator!=(const Dialog::CommandStruct& lhs, const Dialog::CommandStruct& rhs);



#define CTL_ERR (-1)


		struct ControlHWND;


		/**
		* @brief Represents a system control in the UI framework.
		*/
		class LOURDLE_UIFRAMEWORK_API SysControl : public WindowBase
		{
			friend struct ControlHWND;
			SysControl(HWND);
			SysControl();
			SysControl(const SysControl&) = default;
			SysControl(SysControl&&) = default;
		public:
			/**
			* @brief Constructs a SysControl object with the specified parameters.
			*
			* @param Parent The parent window of the control.
			* @param lpClassName The class name of the control.
			* @param wID The ID of the control.
			* @param dwStyle The style of the control.
			* @param bAllowDarkMode Whether to allow dark mode for the control.
			*/
			SysControl(WindowBase* Parent, LPCWSTR lpClassName, WORD wID, DWORD dwStyle, bool bAllowDarkMode);

			/**
			* @brief Constructs a SysControl object with the specified parameters.
			*
			* @param Dlg The dialog that contains the control.
			* @param lpClassName The class name of the control.
			* @param wID The ID of the control.
			* @param dwStyle The style of the control.
			* @param InitProc The initialization procedure for the control.
			*/
			SysControl(Dialog* Dlg, LPCWSTR lpClassName, WORD wID, DWORD dwStyle, void(*InitProc)(SysControl*, Dialog*) = nullptr);

			/**
			* @brief Destructor.
			*/
			virtual ~SysControl();

			/**
			* @brief Sets the font of the control.
			*
			* @param hFont The handle to the font. Default is nullptr.
			* @return HFONT The previous font handle.
			*/
			HFONT SetFont(HFONT hFont = nullptr);

			/**
			* @brief Gets the font of the control.
			*
			* @return HFONT The font handle.
			*/
			HFONT GetFont();

			/**
			* @brief Posts a command message to the parent window.
			*
			* @param wCode The command code.
			* @return bool True if the message was posted successfully, false otherwise.
			*/
			bool PostCommand(WORD wCode);

			/**
			* @brief Gets the ID of the control.
			*
			* @return WORD The ID of the control.
			*/
			WORD GetControlID();
		};

		/**
		* @brief Represents a static control.
		*/
		struct LOURDLE_UIFRAMEWORK_API Static : SysControl
		{
			/**
			* @brief Constructs a Static control with the specified parent window, control ID, and style.
			*
			* @param Parent The parent window of the control.
			* @param wID The control ID.
			* @param dwStyle The control style.
			*/
			Static(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a Static control with the specified parent window and control ID.
			*
			* @param Parent The parent window of the control.
			* @param wID The control ID.
			*/
			Static(Window* Parent, WORD wID = Random());

			/**
			* @brief Constructs a Static control with the specified parent dialog, control ID, and style.
			*
			* @param Parent The parent dialog of the control.
			* @param wID The control ID.
			* @param dwStyle The control style.
			*/
			Static(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a Static control with the specified parent dialog and control ID.
			*
			* @param Parent The parent dialog of the control.
			* @param wID The control ID.
			*/
			Static(Dialog* Parent, WORD wID = Random());

			/**
			* @brief Destructor.
			*/
			~Static() = default;

			/**
			* @brief Creates a Static control from a ControlHWND.
			*
			* @param ref The ControlHWND to create the Static control from.
			* @return Static* The created Static control.
			*/
			static Static* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Retrieves the icon associated with the Static control.
			*
			* @return HICON The handle to the icon.
			*/
			HICON GetIcon();

			/**
			* @brief Sets the icon associated with the Static control.
			*
			* @param hIcon The handle to the icon.
			* @return HICON The previous handle to the icon.
			*/
			HICON SetIcon(HICON hIcon);

			/**
			* @brief Retrieves the image associated with the Static control.
			*
			* @param uType The type of the image.
			* @return HANDLE The handle to the image.
			*/
			HANDLE GetImage(UINT uType);

			/**
			* @brief Sets the image associated with the Static control.
			*
			* @param uType The type of the image.
			* @param hImage The handle to the image.
			* @return HANDLE The previous handle to the image.
			*/
			HANDLE SetImage(UINT uType, HANDLE hImage);

			/**
			* @brief Retrieves the bitmap image associated with the Static control.
			*
			* @return HBITMAP The handle to the bitmap image.
			*/
			HBITMAP GetImageBitmap();

			/**
			* @brief Sets the bitmap image associated with the Static control.
			*
			* @param hBitmap The handle to the bitmap image.
			* @return HBITMAP The previous handle to the bitmap image.
			*/
			HBITMAP SetImageBitmap(HBITMAP hBitmap);

			/**
			* @brief Retrieves the cursor image associated with the Static control.
			*
			* @return HCURSOR The handle to the cursor image.
			*/
			HCURSOR GetImageCursor();

			/**
			* @brief Sets the cursor image associated with the Static control.
			*
			* @param hCursor The handle to the cursor image.
			* @return HCURSOR The previous handle to the cursor image.
			*/
			HCURSOR SetImageCursor(HCURSOR hCursor);

			/**
			* @brief Retrieves the enhanced metafile image associated with the Static control.
			*
			* @return HENHMETAFILE The handle to the enhanced metafile image.
			*/
			HENHMETAFILE GetImageEnhMetaFile();

			/**
			* @brief Sets the enhanced metafile image associated with the Static control.
			*
			* @param hEnhMetaFile The handle to the enhanced metafile image.
			* @return HENHMETAFILE The previous handle to the enhanced metafile image.
			*/
			HENHMETAFILE SetImageEnhMetaFile(HENHMETAFILE hEnhMetaFile);

			/**
			* @brief Retrieves the icon image associated with the Static control.
			*
			* @return HICON The handle to the icon image.
			*/
			HICON GetImageIcon();

			/**
			* @brief Sets the icon image associated with the Static control.
			*
			* @param hIcon The handle to the icon image.
			* @return HICON The previous handle to the icon image.
			*/
			HICON SetImageIcon(HICON hIcon);
		};

		enum class ButtonStyle : BYTE
		{
			_3State,
			Auto3State,
			AutoCheckbox,
			AutoRadioButton,
			Bitmap,
			Checkbox,
			CommandLink,
			DefCommandLink,
			DefPushButton,
			DefSplitButton,
			Icon,
			PushButton,
			RadioButton,
			SplitButton,
			Text
		};

		/**
		* @brief Represents a button control in the UI framework.
		*/
		class LOURDLE_UIFRAMEWORK_API Button : public SysControl
		{
			/**
			* @brief Registers a command for the button control.
			*
			* @tparam Wnd The type of the parent window.
			* @param Parent The parent window.
			* @param CmdProc The command procedure to register.
			* @param wID The ID of the command.
			*/
			template<class Wnd, typename... Args>
			::std::enable_if_t<::std::is_base_of<Window, Wnd>::value>
				RegisterCommand(Wnd* Parent, WORD wID, Args&&... args)
			{
				Parent->Window::RegisterCommand(::std::move<Args>(args)..., hWnd, wID, BN_CLICKED);
			}

			/**
			* @brief Registers a command for the button control.
			*
			* @tparam Dlg The type of the parent dialog.
			* @param Parent The parent dialog.
			* @param CmdProc The command procedure to register.
			* @param wID The ID of the command.
			*/
			template<class Dlg, typename... Args>
			::std::enable_if_t<::std::is_base_of<Dialog, Dlg>::value>
				RegisterCommand(Dlg* Parent, WORD wID, Args&&... args)
			{
				Parent->Dialog::RegisterCommand(::std::move<Args>(args)..., wID, BN_CLICKED);
			}

		public:
			/**
			* @brief Constructs a Button object with the specified parent window, ID, and style.
			*
			* @param Parent The parent window.
			* @param wID The ID of the button control.
			* @param dwStyle The window style of the button control.
			*/
			Button(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a Button object with the specified parent window, ID, and button style.
			*
			* @param Parent The parent window.
			* @param wID The ID of the button control.
			* @param ButtonStyle The style of the button control.
			*/
			Button(Window* Parent, WORD wID, ButtonStyle ButtonStyle = ButtonStyle::Text);

			/**
			* @brief Constructs a Button object with the specified parent window, command handler, style, and ID.
			*
			* @tparam Wnd The type of the parent window.
			* @param Parent The parent window.
			* @param CmdProc The pointer to the command handler method.
			* @param dwStyle The window style of the button control.
			* @param wID The button control ID. Default is a random value.
			*/
			template<class Wnd>
			Button(Wnd* Parent, void(Wnd::* CmdProc)(), DWORD dwStyle, WORD wID = Random()) :
				Button(Parent, wID, dwStyle)
			{
				RegisterCommand(Parent, wID, CmdProc);
			}

			/**
			* @brief Constructs a Button object with the specified parent window, command handler, the button style, and ID.
			*
			* @tparam Wnd The type of the parent window.
			* @param Parent The parent window.
			* @param CmdProc The pointer to the command handler method.
			* @param ButtonStyle The style of the button control.
			* @param wID The button control ID. Default is a random value.
			*/
			template<class Wnd>
			Button(Wnd* Parent, void(Wnd::* CmdProc)(), ButtonStyle ButtonStyle = ButtonStyle::Text, WORD wID = Random()) :
				Button(Parent, wID, ButtonStyle)
			{
				RegisterCommand(Parent, wID, CmdProc);
			}

			/**
			* @brief Constructs a Button object with the specified parent window, style, ID, and additional arguments.
			*
			* @tparam Wnd The type of the parent window.
			* @tparam Args The types of additional arguments passed to the command registrar.
			* @param Parent The parent window.
			* @param dwStyle The style of the button control.
			* @param wID The button control ID. Default is a random value.
			* @param args Additional arguments forwarded to the command registrar.
			*/
			template<class Wnd, typename... Args>
			Button(Wnd* Parent, DWORD dwStyle, WORD wID, Args&&... args) :
				Button(Parent, wID, dwStyle)
			{
				RegisterCommand(Parent, wID, args...);
			}

			/**
			* @brief Constructs a Button object with the specified parent window, style, ID, and additional arguments.
			*
			* @tparam Wnd The type of the parent window.
			* @tparam Args The types of additional arguments passed to the command registrar.
			* @param Parent The parent window.
			* @param ButtonStyle The style of the button control.
			* @param wID The button control ID. Default is a random value.
			* @param args Additional arguments forwarded to the command registrar.
			*/
			template<class Wnd, typename... Args>
			Button(Wnd* Parent, ButtonStyle ButtonStyle, WORD wID, Args&&... args) :
				Button(Parent, wID, ButtonStyle)
			{
				RegisterCommand(Parent, wID, args...);
			}

			/**
			* @brief Constructs a Button object with the specified parent dialog, ID, and style.
			*
			* @param Parent The parent dialog.
			* @param wID The ID of the button control.
			* @param dwStyle The window style of the button control.
			*/
			Button(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a Button object with the specified parent dialog, ID, and button style.
			*
			* @param Parent The parent dialog.
			* @param wID The ID of the button control.
			* @param ButtonStyle The style of the button control.
			*/
			Button(Dialog* Parent, WORD wID, ButtonStyle ButtonStyle = ButtonStyle::Text);

			/**
			* @brief Destructor.
			*/
			~Button() = default;

			/**
			* @brief Creates a Button object from a ControlHWND object.
			*
			* @param ref The ControlHWND object to create from.
			* @return Button* A pointer to the created Button object.
			*/
			static Button* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Simulates a button click.
			*/
			void Click();

			/**
			* @brief Gets the check state of the button control.
			*
			* @return int The check state of the button control.
			*/
			int GetCheck();

			/**
			* @brief Sets the check state of the button control.
			*
			* @param nCheck The check state to set.
			*/
			void SetCheck(int nCheck);

			/**
			* @brief Gets the state of the button control.
			*
			* @return int The state of the button control.
			*/
			int GetState();

			/**
			* @brief Sets the state of the button control.
			*
			* @param bState The state to set.
			*/
			void SetState(bool bState);

			/**
			* @brief Sets the split information for the button control.
			*
			* @param pSplitInfo A pointer to the BUTTON_SPLITINFO structure that contains the split information.
			* @return bool True if the split information is set successfully, false otherwise.
			*/
			bool SetSplitInfo(BUTTON_SPLITINFO* pSplitInfo);

			/**
			* @brief Gets the split information for the button control.
			*
			* @param pSplitInfo A pointer to the BUTTON_SPLITINFO structure to receive the split information.
			* @return bool True if the split information is retrieved successfully, false otherwise.
			*/
			bool GetSplitInfo(BUTTON_SPLITINFO* pSplitInfo);

			/**
			* @brief Sets the drop-down state of the button control.
			*
			* @param fDropDown The drop-down state to set.
			* @return bool True if the drop-down state is set successfully, false otherwise.
			*/
			bool SetDropDownState(bool fDropDown);

			/**
			* @brief Gets the ideal size of the button control.
			*
			* @param pSize A pointer to the SIZE structure to receive the ideal size.
			*/
			void GetIdealSize(PSIZE pSize);

			/**
			* @brief Gets the image associated with the button control.
			*
			* @param uType The type of the image.
			* @return HANDLE The handle to the image.
			*/
			HANDLE GetImage(UINT uType);

			/**
			* @brief Sets the image for the button control.
			*
			* @param uType The type of the image.
			* @param hImage The handle to the image.
			* @return HANDLE The previous handle to the image.
			*/
			HANDLE SetImage(UINT uType, HANDLE hImage);

			/**
			* @brief Gets the bitmap image associated with the button control.
			*
			* @return HBITMAP The handle to the bitmap image.
			*/
			HBITMAP GetImageBitmap();

			/**
			* @brief Sets the bitmap image for the button control.
			*
			* @param hBitmap The handle to the bitmap image.
			* @return HBITMAP The previous handle to the bitmap image.
			*/
			HBITMAP SetImageBitmap(HBITMAP hBitmap);

			/**
			* @brief Gets the icon image associated with the button control.
			*
			* @return HICON The handle to the icon image.
			*/
			HICON GetImageIcon();

			/**
			* @brief Sets the icon image for the button control.
			*
			* @param hIcon The handle to the icon image.
			* @return HICON The previous handle to the icon image.
			*/
			HICON SetImageIcon(HICON hIcon);

			/**
			* @brief Posts a command message to parent window.
			*
			* @return bool True if the command message is posted successfully, false otherwise.
			*/
			bool PostCommand();
		};

		/**
		* @brief Represents an edit control.
		*/
		struct LOURDLE_UIFRAMEWORK_API Edit : public SysControl
		{
			/**
			* @brief Represents an edit control.
			*/
			Edit(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Represents an edit control.
			*/
			Edit(Window* Parent, WORD wID = Random(), bool bMultiline = false);

			/**
			* @brief Represents an edit control.
			*/
			Edit(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Represents an edit control.
			*/
			Edit(Dialog* Parent, WORD wID = Random(), bool bMultiline = false);

			/**
			* @brief Destructor.
			*/
			~Edit() = default;

			/**
			* @brief Creates an Edit object from a ControlHWND object.
			*
			* @param ref The ControlHWND object.
			* @return Edit* The created Edit object.
			*/
			static Edit* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Sets the maximum number of characters that can be entered in the edit control.
			*
			* @param nLength The maximum number of characters.
			*/
			void SetLimitText(int nLength);

			/**
			* @brief Sets the selection in the edit control.
			*
			* @param ichStart The starting position of the selection.
			* @param ichEnd The ending position of the selection.
			*/
			void SetSel(int ichStart, int ichEnd);

			/**
			* @brief Replaces the selected text in the edit control with the specified text.
			*
			* @param pReplace The text to replace the selected text with.
			*/
			void ReplaceSel(PCWSTR pReplace);

			/**
			* @brief Sets the read-only state of the edit control.
			*
			* @param bReadOnly Whether the edit control should be read-only or not. Default is true.
			* @return bool True if the read-only state is set successfully, false otherwise.
			*/
			bool SetReadOnly(bool bReadOnly = true);
		};

		/**
		* @brief Represents a list box control.
		*/
		struct LOURDLE_UIFRAMEWORK_API ListBox : public SysControl
		{
			/**
			* @brief Represents a list box control.
			*/
			ListBox(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Represents a list box control.
			*/
			ListBox(Window* Parent, WORD wID = Random());

			/**
			* @brief Represents a list box control.
			*/
			ListBox(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Represents a list box control.
			*/
			ListBox(Dialog* Parent, WORD wID = Random());

			/**
			* @brief Destructor.
			*/
			~ListBox() = default;

			/**
			* @brief Creates a ListBox object from a ControlHWND object.
			*
			* @param ref The ControlHWND object.
			* @return ListBox* The created ListBox object.
			*/
			static ListBox* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Adds a string to the list box.
			*
			* @param pString The string to add.
			*/
			void AddString(PCWSTR pString);

			/**
			* @brief Inserts a string into the list box at the specified index.
			*
			* @param pString The string to insert.
			* @param nIndex The index at which to insert the string.
			*/
			void InsertString(PCWSTR pString, int nIndex);

			/**
			* @brief Deletes the string at the specified index from the list box.
			*
			* @param nIndex The index of the string to delete.
			* @return int The index of the next string after the deleted string.
			*/
			int DeleteString(int nIndex);

			/**
			* @brief Gets the number of strings in the list box.
			*
			* @return int The number of strings in the list box.
			*/
			int GetCount();

			/**
			* @brief Sets the currently selected item in the list box.
			*
			* @param nIndex The index of the item to select.
			* @return int The index of the previously selected item.
			*/
			int SetCurSel(int nIndex);

			/**
			* @brief Gets the index of the currently selected item in the list box.
			*
			* @return int The index of the currently selected item.
			*/
			int GetCurSel();

			/**
			* @brief Finds the first string in the list box that matches the specified string.
			*
			* @param pString The string to search for.
			* @param nIndexStart The index at which to start the search. Default is 0.
			* @return int The index of the found string, or LB_ERR if no match is found.
			*/
			int FindString(PCWSTR pString, int nIndexStart = 0);

			/**
			* @brief Gets the length of the string at the specified index in the list box.
			*
			* @param nIndex The index of the string.
			* @return int The length of the string.
			*/
			int GetTextLength(int nIndex);

			/**
			* @brief Gets the string at the specified index in the list box.
			*
			* @param nIndex The index of the string.
			* @param pszBuffer The buffer to receive the string.
			* @return int The length of the string.
			*/
			int GetText(int nIndex, PWSTR pszBuffer);

			/**
			* @brief Gets the string at the specified index in the list box.
			*
			* @param nIndex The index of the string.
			* @return String The string at the specified index.
			*/
			String GetText(int nIndex);
		};

		/**
		* @brief Represents a combo box control.
		*/
		struct LOURDLE_UIFRAMEWORK_API ComboBox : public SysControl
		{
			/**
			* @brief Constructs a combo box control with the specified parent window, control ID, and style.
			*
			* @param Parent The parent window of the combo box.
			* @param wID The control ID of the combo box.
			* @param dwStyle The style of the combo box.
			*/
			ComboBox(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a combo box control with the specified parent window and control ID.
			*
			* @param Parent The parent window of the combo box.
			* @param wID The control ID of the combo box.
			*/
			ComboBox(Window* Parent, WORD wID = Random());

			/**
			* @brief Constructs a combo box control with the specified parent dialog, control ID, and style.
			*
			* @param Parent The parent dialog of the combo box.
			* @param wID The control ID of the combo box.
			* @param dwStyle The style of the combo box.
			*/
			ComboBox(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a combo box control with the specified parent dialog and control ID.
			*
			* @param Parent The parent dialog of the combo box.
			* @param wID The control ID of the combo box.
			*/
			ComboBox(Dialog* Parent, WORD wID = Random());

			/**
			* @brief Destructor.
			*/
			~ComboBox() = default;

			/**
			* @brief Creates a ComboBox object from a ControlHWND object.
			*
			* @param ref The ControlHWND object representing the combo box control.
			* @return ComboBox* A pointer to the created ComboBox object.
			*/
			static ComboBox* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Adds a string to the combo box.
			*
			* @param pString The string to add.
			*/
			void AddString(PCWSTR pString);

			/**
			* @brief Inserts a string into the combo box at the specified index.
			*
			* @param pString The string to insert.
			* @param nIndex The index at which to insert the string.
			*/
			void InsertString(PCWSTR pString, int nIndex);

			/**
			* @brief Deletes the string at the specified index from the combo box.
			*
			* @param nIndex The index of the string to delete.
			* @return int The index of the next string after the deleted string.
			*/
			int DeleteString(int nIndex);

			/**
			* @brief Finds the index of the first occurrence of the specified string in the combo box.
			*
			* @param pString The string to find.
			* @param nIndexStart The index at which to start the search. Default is -1, which means the search starts from the beginning.
			* @return int The index of the found string, or -1 if the string is not found.
			*/
			int FindString(PCWSTR pString, int nIndexStart = -1);

			/**
			* @brief Gets the number of strings in the combo box.
			*
			* @return int The number of strings in the combo box.
			*/
			int GetCount();

			/**
			* @brief Gets the length of the text of the string at the specified index in the combo box.
			*
			* @param nIndex The index of the string.
			* @return int The length of the text.
			*/
			int GetTextLength(int nIndex);

			/**
			* @brief Gets the text of the string at the specified index in the combo box.
			*
			* @param nIndex The index of the string.
			* @param pszBuffer The buffer to receive the text.
			* @return int The length of the text.
			*/
			int GetText(int nIndex, PWSTR pszBuffer);

			/**
			* @brief Gets the text of the string at the specified index in the combo box.
			*
			* @param nIndex The index of the string.
			* @return String The text of the string.
			*/
			String GetText(int nIndex);

			/**
			* @brief Sets the currently selected item in the combo box.
			*
			* @param nIndex The index of the item to select.
			* @return int The index of the previously selected item, or CB_ERR if an error occurs.
			*/
			int SetCurSel(int nIndex);

			/**
			* @brief Gets the index of the currently selected item in the combo box.
			*
			* @return int The index of the currently selected item, or CB_ERR if no item is selected or an error occurs.
			*/
			int GetCurSel();
		};

		/**
		* @brief Represents a ListView control.
		*/
		struct LOURDLE_UIFRAMEWORK_API ListView : public SysControl
		{
			/**
			* @brief Constructs a ListView control with the specified parent window, control ID, and style.
			*
			* @param Parent The parent window.
			* @param wID The control ID.
			* @param dwStyle The control style.
			*/
			ListView(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a ListView control with the specified parent window and control ID.
			*
			* @param Parent The parent window.
			* @param wID The control ID.
			* @param bCheckBoxes Whether to enable checkboxes or not.
			*/
			ListView(Window* Parent, WORD wID = Random(), bool bCheckBoxes = false);

			/**
			* @brief Constructs a ListView control with the specified parent dialog and control ID.
			*
			* @param Parent The parent dialog.
			* @param wID The control ID.
			*/
			ListView(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Destructor.
			*/
			~ListView() = default;

			/**
			* @brief Creates a ListView control from a ControlHWND object.
			*
			* @param ref The ControlHWND object.
			* @return ListView* The created ListView control.
			*/
			static ListView* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Adds an extended ListView style to the control.
			*
			* @param dwExtendedStyle The extended style to add.
			* @return DWORD The updated extended style.
			*/
			DWORD AddExtendedListViewStyle(DWORD dwExtendedStyle);

			/**
			* @brief Removes an extended ListView style from the control.
			*
			* @param dwExtendedStyle The extended style to remove.
			* @return DWORD The updated extended style.
			*/
			DWORD RemoveExtendedListViewStyle(DWORD dwExtendedStyle);

			/**
			* @brief Sets the extended ListView style for the control.
			*
			* @param dwExtendedStyle The extended style to set.
			* @return DWORD The updated extended style.
			*/
			DWORD SetExtendedListViewStyle(DWORD dwExtendedStyle);

			/**
			* @brief Gets the extended ListView style of the control.
			*
			* @return DWORD The extended ListView style.
			*/
			DWORD GetExtendedListViewStyle();

			/**
			* @brief Inserts a column into the ListView control.
			*
			* @param pColumnName The name of the column.
			* @param Width The width of the column.
			* @param iColumnIndex The index of the column.
			* @return int The index of the inserted column.
			*/
			int InsertColumn(PCWSTR pColumnName, int Width, int iColumnIndex);

			/**
			* @brief Inserts a column into the ListView control.
			*
			* @param pColumnName The name of the column.
			* @param Width The width of the column.
			* @param iColumnIndex The index of the column.
			* @return int The index of the inserted column.
			*/
			int InsertColumn(PWSTR pColumnName, int Width, int iColumnIndex);

			/**
			* @brief Deletes a column from the ListView control.
			*
			* @param iColumnIndex The index of the column to delete.
			* @return bool True if the column is deleted successfully, false otherwise.
			*/
			bool DeleteColumn(int iColumnIndex);

			/**
			* @brief Gets the number of items in the ListView control.
			*
			* @return int The number of items.
			*/
			int GetItemCount();

			/**
			* @brief Inserts a new item into the ListView control.
			*
			* @return int The index of the inserted item.
			*/
			int InsertItem();

			/**
			* @brief Sets the text of a subitem in the ListView control.
			*
			* @param iItemIndex The index of the item.
			* @param iSubItem The index of the subitem.
			* @param pItemText The text to set.
			*/
			void SetItemText(int iItemIndex, int iSubItem, PCWSTR pItemText);

			/**
			* @brief Sets the text of a subitem in the ListView control.
			*
			* @param iItemIndex The index of the item.
			* @param iSubItem The index of the subitem.
			* @param pItemText The text to set.
			*/
			void SetItemText(int iItemIndex, int iSubItem, PWSTR pItemText);

			/**
			* @brief Gets the text of a subitem in the ListView control.
			*
			* Only gets the first 260 characters of the text.
			*
			* @param iIndex The index of the item.
			* @param iSubItem The index of the subitem.
			* @return String The text of the subitem.
			*/
			String GetItemText(int iIndex, int iSubItem);

			/**
			* @brief Gets the text of a subitem in the ListView control.
			*
			* @param iIndex The index of the item.
			* @param iSubItem The index of the subitem.
			* @param pszText The buffer to receive the text.
			* @param cchTextMax The maximum number of characters to copy to the buffer.
			* @return int The number of characters copied to the buffer.
			*/
			int GetItemText(int iIndex, int iSubItem, PWSTR pszText, int cchTextMax);

			/**
			* @brief Deletes an item from the ListView control.
			*
			* @param iItemIndex The index of the item to delete.
			* @return bool True if the item is deleted successfully, false otherwise.
			*/
			bool DeleteItem(int iItemIndex);

			/**
			* @brief Deletes all items from the ListView control.
			*
			* @return bool True if all items are deleted successfully, false otherwise.
			*/
			bool DeleteAllItems();

			/**
			* @brief Gets the index of the selected column in the ListView control.
			*
			* @return UINT The index of the selected column.
			*/
			UINT GetSelectedColumn();

			/**
			* @brief Gets the number of selected items in the ListView control.
			*
			* @return UINT The number of selected items.
			*/
			UINT GetSelectedCount();

			/**
			* @brief Gets the index of the selection mark in the ListView control.
			*
			* @return int The index of the selection mark.
			*/
			int GetSelectionMark();

			/**
			* @brief Sets the index of the selection mark in the ListView control.
			*
			* @param iItemIndex The index of the item to set as the selection mark.
			* @return int The previous index of the selection mark.
			*/
			int SetSelectionMark(int iItemIndex);

			/**
			* @brief Gets the state of an item in the ListView control.
			*
			* @param iIndex The index of the item.
			* @param uMask The mask that specifies which state flags to retrieve.
			* @return UINT The state of the item.
			*/
			UINT GetItemState(int iIndex, UINT uMask);

			/**
			* @brief Sets the state of an item in the ListView control.
			*
			* @param iIndex The index of the item.
			* @param uData The new state of the item.
			* @param uMask The mask that specifies which state flags to change.
			* @return bool True if the state is set successfully, false otherwise.
			*/
			bool SetItemState(int iIndex, UINT uData, UINT uMask);

			/**
			* @brief Gets the check state of an item in the ListView control.
			*
			* @param iItemIndex The index of the item.
			* @return int The check state of the item. 0 for unchecked, 1 for checked.
			*/
			int GetCheckState(int iItemIndex);

			/**
			* @brief Sets the check state of an item in the ListView control.
			*
			* @param iItemIndex The index of the item.
			* @param fCheck The check state. True for checked, false for unchecked.
			*/
			void SetCheckState(int iItemIndex, bool fCheck);
		};

		/**
		* @brief Represents a tree view control.
		*/
		struct LOURDLE_UIFRAMEWORK_API TreeView : public SysControl
		{
			/**
			* @brief Constructs a TreeView object with the specified parent window, control ID, and style.
			*
			* @param Parent The parent window of the tree view control.
			* @param wID The control ID of the tree view control.
			* @param dwStyle The style of the tree view control.
			*/
			TreeView(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a TreeView object with the specified parent window, control ID, and style.
			*
			* @param Parent The parent window of the tree view control.
			* @param wID The control ID of the tree view control.
			* @param bHasButtons Whether the tree view control has buttons or not.
			* @param bCheckBoxes Whether the tree view control has checkboxes or not.
			*/
			TreeView(Window* Parent, WORD wID = Random(), bool bHasButtons = true, bool bCheckBoxes = false);

			/**
			* @brief Constructs a TreeView object with the specified parent dialog, control ID, and style.
			*
			* @param Parent The parent dialog of the tree view control.
			* @param wID The control ID of the tree view control.
			* @param dwStyle The style of the tree view control.
			*/
			TreeView(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a TreeView object with the specified parent dialog, control ID, and style.
			*
			* @param Parent The parent dialog of the tree view control.
			* @param wID The control ID of the tree view control.
			* @param bHasButtons Whether the tree view control has buttons or not.
			* @param bCheckBoxes Whether the tree view control has checkboxes or not.
			*/
			TreeView(Dialog* Parent, WORD wID = Random(), bool bHasButtons = true, bool bCheckBoxes = false);

			/**
			* @brief Destructor.
			*/
			~TreeView() = default;

			/**
			* @brief Creates a TreeView object from a ControlHWND object.
			*
			* @param ref The ControlHWND object representing the tree view control.
			* @return TreeView* A pointer to the TreeView object.
			*/
			static TreeView* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Enumeration of the possible check states for a tree view item.
			*/
			enum CheckState : UINT
			{
				Unchecked = 0,
				Checked,
				PartialChecked,
				DimmedChecked,
				ExclusionChecked
			};

			/**
			* @brief Checks if the state of a checkbox is changing.
			*
			* This method is called in the OnNotify method and assumes that the TreeView object has checkboxes related style.
			*
			* @param pNMHDR A pointer to the NMHDR structure.
			* @return HTREEITEM The handle to the tree item if the checkbox state is changing, otherwise NULL.
			*/
			HTREEITEM IsCheckBoxStateChanging(LPNMHDR pNMHDR);

			/**
			* @brief Automatically manages the state of tree checkboxes.
			*
			* This method is called in the OnNotify method. The TreeView object must have the TVS_EX_PARTIALCHECKBOXES extended style and not the TVS_CHECKBOXES style.
			* This method assumes that the object meets the conditions.
			*
			* @param pNMHDR A pointer to the NMHDR structure that contains information about the notification.
			* @return bool Returns true if the tree checkbox state was successfully managed, otherwise returns false.
			*/
			bool AutoManageTreeCheckboxes(LPNMHDR pNMHDR);

			/**
			* @brief Deletes the specified item from the tree view control.
			*
			* @param hItem The handle to the item to delete.
			* @return bool Returns true if the item was successfully deleted, otherwise returns false.
			*/
			bool DeleteItem(HTREEITEM hItem);

			/**
			* @brief Expands or collapses the specified item in the tree view control.
			*
			* @param hItem The handle to the item to expand or collapse.
			* @param uFlags The flags that specify how to expand or collapse the item.
			* @return bool Returns true if the item was successfully expanded or collapsed, otherwise returns false.
			*/
			bool Expand(HTREEITEM hItem, UINT uFlags);

			/**
			* @brief Gets the background color of the tree view control.
			*
			* @return COLORREF The background color of the tree view control.
			*/
			COLORREF GetBkColor();

			/**
			* @brief Gets the handle to the first child item of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @return HTREEITEM The handle to the first child item of the specified item.
			*/
			HTREEITEM GetChild(HTREEITEM hItem);

			/**
			* @brief Gets the number of items in the tree view control.
			*
			* @return UINT The number of items in the tree view control.
			*/
			UINT GetCount();

			/**
			* @brief Gets the check state of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @return CheckState The check state of the specified item.
			*/
			CheckState GetCheckState(HTREEITEM hItem);

			/**
			* @brief Gets the handle to the edit control of the tree view control.
			*
			* @return ControlHWND The handle to the edit control.
			*/
			ControlHWND GetEditControl();

			/**
			* @brief Gets the edit control of the tree view control.
			*
			* @param ref The ControlHWND object representing the edit control.
			* @return Edit* A pointer to the Edit control.
			*/
			Edit* GetEditControl(ControlHWND& ref);

			/**
			* @brief Gets the extended style of the tree view control.
			*
			* @return DWORD The extended style of the tree view control.
			*/
			DWORD GetExtendedStyle();

			/**
			* @brief Gets the color of the insert mark in the tree view control.
			*
			* @return COLORREF The color of the insert mark.
			*/
			COLORREF GetInsertMarkColor();

			/**
			* @brief Gets the attributes of the specified item in the tree view control.
			*
			* @param pItem A pointer to the TVITEMEXW structure that receives the attributes of the item.
			* @return bool Returns true if the attributes were successfully retrieved, otherwise returns false.
			*/
			bool GetItem(TVITEMEXW* pItem);

			/**
			* @brief Gets the state of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @param uMask The mask that specifies which state bits to retrieve.
			* @return UINT The state of the specified item.
			*/
			UINT GetItemState(HTREEITEM hItem, UINT uMask);

			/**
			* @brief Gets the text of the specified item in the tree view control.
			*
			* Only gets the first 260 characters of the text.
			*
			* @param hItem The handle to the item.
			* @return String The text of the specified item.
			*/
			String GetItemText(HTREEITEM hItem);

			/**
			* @brief Gets the text of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @param pszText The buffer to receive the text.
			* @param cchTextMax The maximum number of characters to copy to the buffer.
			* @return bool Returns true if the text was successfully retrieved, otherwise returns false.
			*/
			bool GetItemText(HTREEITEM hItem, PWSTR pszText, int cchTextMax);

			/**
			* @brief Gets the handle to the next item in the tree view control.
			*
			* @param hItem The handle to the current item.
			* @param uFlags The flags that specify how to retrieve the next item.
			* @return HTREEITEM The handle to the next item.
			*/
			HTREEITEM GetNextItem(HTREEITEM hItem, UINT uFlags);

			/**
			* @brief Gets the handle to the next sibling item of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @return HTREEITEM The handle to the next sibling item of the specified item.
			*/
			HTREEITEM GetNextSibling(HTREEITEM hItem);

			/**
			* @brief Gets the handle to the parent item of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @return HTREEITEM The handle to the parent item of the specified item.
			*/
			HTREEITEM GetParent(HTREEITEM hItem);

			/**
			* @brief Gets the handle to the previous sibling item of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @return HTREEITEM The handle to the previous sibling item of the specified item.
			*/
			HTREEITEM GetPrevSibling(HTREEITEM hItem);

			/**
			* @brief Gets the handle to the root item of the tree view control.
			*
			* @return HTREEITEM The handle to the root item of the tree view control.
			*/
			HTREEITEM GetRoot();

			/**
			* @brief Gets the handle to the selected item in the tree view control.
			*
			* @return HTREEITEM The handle to the selected item.
			*/
			HTREEITEM GetSelection();

			/**
			* @brief Determines which item, if any, is at the specified position in the tree view control.
			*
			* @param pHitTestInfo A pointer to the TVHITTESTINFO structure that contains the position to test.
			* @return HTREEITEM The handle to the item at the specified position.
			*/
			HTREEITEM HitTest(LPTVHITTESTINFO pHitTestInfo);

			/**
			* @brief Inserts an item into the tree view control.
			*
			* @param lpInsertStruct A pointer to the TVINSERTSTRUCTW structure that contains information about the item to insert.
			* @return HTREEITEM The handle to the inserted item.
			*/
			HTREEITEM InsertItem(LPTVINSERTSTRUCTW lpInsertStruct);

			/**
			* @brief Inserts an item into the tree view control.
			*
			* @param pszText The text of the item to insert.
			* @param hParent The handle to the parent item. Default is nullptr.
			* @param hInsertAfter The handle to the item after which the new item is inserted. Default is nullptr.
			* @return HTREEITEM The handle to the inserted item.
			*/
			HTREEITEM InsertItem(PCWSTR pszText, HTREEITEM hParent = nullptr, HTREEITEM hInsertAfter = nullptr);

			/**
			* @brief Inserts an item into the tree view control.
			*
			* @param pszText The text of the item to insert.
			* @param hParent The handle to the parent item. Default is nullptr.
			* @param hInsertAfter The handle to the item after which the new item is inserted. Default is nullptr.
			* @return HTREEITEM The handle to the inserted item.
			*/
			HTREEITEM InsertItem(PWSTR pszText, HTREEITEM hParent = nullptr, HTREEITEM hInsertAfter = nullptr);

			/**
			* @brief Sets the background color of the tree view control.
			*
			* @param Color The new background color.
			* @return COLORREF The previous background color.
			*/
			COLORREF SetBkColor(COLORREF Color);

			/**
			* @brief Sets the check state of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @param State The new check state.
			* @return bool Returns true if the check state was successfully set, otherwise returns false.
			*/
			bool SetCheckState(HTREEITEM hItem, CheckState State);

			/**
			* @brief Sets the extended style of the tree view control.
			*
			* @param dwExStyle The new extended style.
			* @return HRESULT Returns S_OK if the extended style was successfully set, otherwise returns an error code.
			*/
			HRESULT SetExtendedStyle(DWORD dwExStyle);

			/**
			* @brief Sets the extended style of the tree view control.
			*
			* @param dwExStyle The new extended style.
			* @param dwMask The mask that specifies which extended style bits to set.
			* @return HRESULT Returns S_OK if the extended style was successfully set, otherwise returns an error code.
			*/
			HRESULT SetExtendedStyle(DWORD dwExStyle, DWORD dwMask);

			/**
			* @brief Sets the color of the insert mark in the tree view control.
			*
			* @param Color The new color of the insert mark.
			* @return bool Returns true if the color of the insert mark was successfully set, otherwise returns false.
			*/
			bool SetInsertMarkColor(COLORREF Color);

			/**
			* @brief Sets the attributes of the specified item in the tree view control.
			*
			* @param pItem A pointer to the TVITEMEXW structure that contains the new attributes of the item.
			* @return bool Returns true if the attributes were successfully set, otherwise returns false.
			*/
			bool SetItem(TVITEMEXW* pItem);

			/**
			* @brief Sets the state of the specified item in the tree view control.
			*
			* @param hItem The handle to the item.
			* @param uMask The mask that specifies which state bits to set.
			* @param uState The new state of the item.
			* @return bool Returns true if the state was successfully set, otherwise returns false.
			*/
			bool SetItemState(HTREEITEM hItem, UINT uMask, UINT uState);
		};

		/**
		* @brief Represents a progress bar control.
		*/
		struct LOURDLE_UIFRAMEWORK_API ProgressBar : public SysControl
		{
			/**
			* @brief Constructs a ProgressBar control with the specified parent window, control ID, and style.
			*
			* @param Parent The parent window of the control.
			* @param wID The control ID.
			* @param dwStyle The control style.
			*/
			ProgressBar(Window* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a ProgressBar control with the specified parent window and control ID.
			*
			* @param Parent The parent window of the control.
			* @param wID The control ID.
			*/
			ProgressBar(Window* Parent, WORD wID = Random());

			/**
			* @brief Constructs a ProgressBar control with the specified parent dialog, control ID, and style.
			*
			* @param Parent The parent dialog of the control.
			* @param wID The control ID.
			* @param dwStyle The control style.
			*/
			ProgressBar(Dialog* Parent, WORD wID, DWORD dwStyle);

			/**
			* @brief Constructs a ProgressBar control with the specified parent dialog and control ID.
			*
			* @param Parent The parent dialog of the control.
			* @param wID The control ID.
			*/
			ProgressBar(Dialog* Parent, WORD wID = Random());

			/**
			* @brief Destructor.
			*/
			~ProgressBar() = default;

			/**
			* @brief Creates a ProgressBar control from an existing ControlHWND.
			*
			* @param ref The ControlHWND to create the ProgressBar control from.
			* @return ProgressBar* The created ProgressBar control.
			*/
			static ProgressBar* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Sets the ProgressBar control to display a marquee progress bar.
			*
			* @param bMarquee Whether to display a marquee progress bar or not. Default is true.
			*/
			void SetMarqueeProgressBar(bool bMarquee = true);

			/**
			* @brief Sets the range of the ProgressBar control.
			*
			* @param iLow The lower bound of the range.
			* @param iHigh The upper bound of the range.
			*/
			void SetRange(int iLow, int iHigh);

			/**
			* @brief Gets the range of the ProgressBar control.
			*
			* @param pRange A pointer to a PBRANGE structure to receive the range.
			*/
			void GetRange(PPBRANGE pRange);

			/**
			* @brief Sets the color of the progress bar.
			*
			* @param Color The color to set. Default is CLR_DEFAULT.
			* @return COLORREF The previous color of the progress bar.
			*/
			COLORREF SetBarColor(COLORREF Color = CLR_DEFAULT);

			/**
			* @brief Gets the color of the progress bar.
			*
			* @return COLORREF The color of the progress bar.
			*/
			COLORREF GetBarColor();

			/**
			* @brief Sets the background color of the progress bar.
			*
			* @param Color The color to set. Default is CLR_DEFAULT.
			* @return COLORREF The previous background color of the progress bar.
			*/
			COLORREF SetBkColor(COLORREF Color = CLR_DEFAULT);

			/**
			* @brief Gets the background color of the progress bar.
			*
			* @return COLORREF The background color of the progress bar.
			*/
			COLORREF GetBkColor();

			enum State
			{
				Normal = 1,
				Error,
				Paused
			};

			/**
			* @brief Sets the state of the progress bar.
			*
			* @param state The state to set.
			* @return State The previous state of the progress bar.
			*/
			State SetState(State state);

			/**
			* @brief Gets the state of the progress bar.
			*
			* @return State The state of the progress bar.
			*/
			State GetState();

			/**
			* @brief Sets the position of the progress bar.
			*
			* @param NewPos The new position.
			* @return LONG The previous position of the progress bar.
			*/
			LONG SetPos(LONG NewPos);

			/**
			* @brief Gets the position of the progress bar.
			*
			* @return LONG The position of the progress bar.
			*/
			LONG GetPos();

			/**
			* @brief Increases or decreases the position of the progress bar by the specified amount.
			*
			* @param DeltaPos The amount to increase or decrease the position.
			* @return LONG The new position of the progress bar.
			*/
			LONG DeltaPos(LONG DeltaPos);

			/**
			* @brief Sets the step size of the progress bar.
			*
			* @param NewStep The new step size.
			* @return LONG The previous step size of the progress bar.
			*/
			LONG SetStep(LONG NewStep);

			/**
			* @brief Advances the position of the progress bar by the step size.
			*
			* @return LONG The new position of the progress bar.
			*/
			LONG StepIt();
		};

		/**
		* @brief Represents a scroll bar control.
		*/
		struct LOURDLE_UIFRAMEWORK_API ScrollBar : public SysControl
		{
			/**
			* @brief Constructs a ScrollBar object with the specified parent window and style.
			*
			* @param Parent The parent window.
			* @param dwStyle The style of the scroll bar control.
			*/
			ScrollBar(Window* Parent, DWORD dwStyle);

			/**
			* @brief Constructs a ScrollBar object with the specified parent window and orientation.
			*
			* @param Parent The parent window.
			* @param Vert Whether the scroll bar control is vertical or not. Default is true.
			*/
			ScrollBar(Window* Parent, bool Vert = true);

			/**
			* @brief Destructor.
			*/
			~ScrollBar() = default;

			/**
			* @brief Creates a ScrollBar object from a ControlHWND object.
			*
			* @param ref The ControlHWND object.
			* @return ScrollBar* A pointer to the created ScrollBar object.
			*/
			static ScrollBar* FromControlHWND(ControlHWND& ref);

			/**
			* @brief Enables or disables the scroll bar control's arrows.
			*
			* @param wArrows The arrow flags. See MSDN documentation for more details.
			* @return bool True if successful, false otherwise.
			*/
			bool EnableScrollBar(UINT wArrows);

			/**
			* @brief Retrieves information about the scroll bar control.
			*
			* @param psbi A pointer to a SCROLLBARINFO structure that receives the information.
			* @return bool True if successful, false otherwise.
			*/
			bool GetScrollBarInfo(LPSCROLLBARINFO psbi);

			/**
			* @brief Retrieves information about the scroll bar control's parameters.
			*
			* @param lpsi A pointer to a SCROLLINFO structure that receives the information.
			* @return bool True if successful, false otherwise.
			*/
			bool GetScrollInfo(LPSCROLLINFO lpsi);

			/**
			* @brief Sets the scroll bar control's parameters.
			*
			* @param lpsi A pointer to a SCROLLINFO structure that contains the new parameters.
			* @param bRedraw Whether to redraw the scroll bar control. Default is true.
			* @return bool True if successful, false otherwise.
			*/
			bool SetScrollInfo(LPSCROLLINFO lpsi, bool bRedraw = true);

			/**
			* @brief Retrieves the current position of the scroll box in the scroll bar control.
			*
			* @return int The current position.
			*/
			int GetScrollPos();

			/**
			* @brief Sets the position of the scroll box in the scroll bar control.
			*
			* @param nPos The new position.
			* @param bRedraw Whether to redraw the scroll bar control. Default is true.
			* @return int The previous position.
			*/
			int SetScrollPos(int nPos, bool bRedraw = true);

			/**
			* @brief Retrieves the minimum and maximum positions of the scroll bar control.
			*
			* @param lpMinPos A pointer to the variable that receives the minimum position.
			* @param lpMaxPos A pointer to the variable that receives the maximum position.
			* @return bool True if successful, false otherwise.
			*/
			bool GetScrollRange(LPINT lpMinPos, LPINT lpMaxPos);

			/**
			* @brief Sets the minimum and maximum positions of the scroll bar control.
			*
			* @param nMinPos The new minimum position.
			* @param nMaxPos The new maximum position.
			* @param bRedraw Whether to redraw the scroll bar control. Default is true.
			* @return bool True if successful, false otherwise.
			*/
			bool SetScrollRange(int nMinPos, int nMaxPos, bool bRedraw = true);

			/**
			* @brief Shows or hides the scroll bar control.
			*
			* @param bShow Whether to show or hide the scroll bar control. Default is true.
			* @return bool True if successful, false otherwise.
			*/
			bool ShowScrollBar(bool bShow = true);
		};

		/**
		* @brief Represents a tooltips control.
		* All other controls in this namespace forward the notifications they receive from Tooltips to the parent window.
		*/
		struct LOURDLE_UIFRAMEWORK_API Tooltips : SysControl
		{
			/**
			* @brief Constructs a tooltips control with the specified style.
			*
			* @param dwStyle The style of the tooltips control.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			Tooltips(DWORD dwStyle, bool bAllowDarkMode = true);

			/**
			* @brief Constructs a tooltips control with the specified balloon style.
			*
			* @param bBalloon Whether the tooltips control should be a balloon or not.
			* @param bAllowDarkMode Whether to allow dark mode or not. Default is true.
			*/
			Tooltips(bool bBalloon = false, bool bAllowDarkMode = true);

			/**
			* @brief Destructor.
			*/
			~Tooltips() = default;

			/**
			* @brief Activates or deactivates the tooltips control.
			*
			* @param bActivate Whether to activate or deactivate the tooltips control. Default is true.
			*/
			void Activate(bool bActivate = true);

			/**
			* @brief Adds a tool to the tooltips control.
			*
			* @param pToolInfo The tool information.
			* @return bool True if the tool was added successfully, false otherwise.
			*/
			bool AddTool(LPTTTOOLINFOW pToolInfo);

			/**
			* @brief Deletes a tool from the tooltips control.
			*
			* @param hWnd The handle to the window that owns the tool.
			* @param uID The ID of the tool.
			*/
			void DelTool(HWND hWnd, UINT_PTR uID);

			/**
			* @brief Enumerates the tools in the tooltips control.
			*
			* @param wId The ID of the tool to enumerate.
			* @param pToolInfo The tool information.
			*/
			void EnumTools(WORD wId, LPTTTOOLINFOW pToolInfo);

			/**
			* @brief Pops up a tooltip.
			*/
			void Pop();

			/**
			* @brief Pops up the tooltips control.
			*/
			void PopUp();

			/**
			* @brief Relays an event to the tooltips control.
			*
			* @param ExtraInfo The extra information for the event.
			* @param lpMsg The message to relay.
			*/
			void RelayEvent(WPARAM ExtraInfo, LPMSG lpMsg);

			/**
			* @brief Sets the delay time for the tooltips control.
			*
			* @param uFlag The delay time flag.
			* @param wDelayTime The delay time value.
			*/
			void SetDelayTime(UINT uFlag, WORD wDelayTime);

			/**
			* @brief Sets the title of the window with an optional icon.
			*
			* @param pszTitle The title to set.
			* @param uIcon The icon to display. Default is TTI_NONE.
			* @return bool True if the title is set successfully, false otherwise.
			*/
			bool SetTitle(PCWSTR pszTitle, UINT_PTR uIcon = TTI_NONE);

			/**
			* @brief Sets the title of the window with a specified icon.
			*
			* @param pszTitle The title to set.
			* @param hIcon The icon handle to display.
			* @return bool True if the title is set successfully, false otherwise.
			*/
			bool SetTitle(PCWSTR pszTitle, HICON hIcon);

			/**
			* @brief Gets the maximum width of the tip.
			*
			* @return int The maximum width of the tip.
			*/
			int GetMaxTipWidth();
			
			/**
			* @brief Sets the maximum width of the tip.
			*
			* @param iWidth The maximum width of the tip.
			* @return int The previous maximum width of the tip.
			*/
			int SetMaxTipWidth(int iWidth);

			/**
			* @brief Gets the background color of the tip.
			*
			* @return COLORREF The background color of the tip.
			*/
			COLORREF GetTipBkColor();

			/**
			* @brief Sets the background color of the tip.
			*
			* @param Color The background color of the tip.
			* @return COLORREF The previous background color of the tip.
			*/
			COLORREF SetTipBkColor(COLORREF Color);

			/**
			* @brief Gets the text color of the tip.
			*
			* @return COLORREF The text color of the tip.
			*/
			COLORREF GetTipTextColor();

			/**
			* @brief Sets the text color of the tip.
			*
			* @param Color The text color of the tip.
			* @return COLORREF The previous text color of the tip.
			*/
			COLORREF SetTipTextColor(COLORREF Color);

			/**
			* @brief Retrieves the Tooltips object associated with the specified ControlHWND object.
			*
			* @param ref The ControlHWND object.
			* @return Tooltips* The Tooltips object associated with the ControlHWND object.
			*/
			static Tooltips* FromControlHWND(ControlHWND& ref);
		};

		
		/**
		* @brief Represents a common control.
		*/
		struct LOURDLE_UIFRAMEWORK_API ControlHWND : SysControl
		{
			/**
			* @brief Represents a control window handle.
			*/
			ControlHWND() = default;

			/**
			* @brief Constructs a ControlHWND object from a WindowBase object.
			*
			* @param window The WindowBase object.
			*/
			ControlHWND(WindowBase& window);

			/**
			* @brief Constructs a ControlHWND object from a WindowBase pointer.
			*
			* @param window The WindowBase pointer.
			*/
			ControlHWND(WindowBase* window);

			/**
			* @brief Constructs a ControlHWND object from a HWND.
			*
			* @param hWnd The HWND.
			*/
			ControlHWND(HWND hWnd);

			/**
			* @brief Move constructor.
			*
			* @param other The ControlHWND object to move from.
			*/
			ControlHWND(ControlHWND&&) = default;

			/**
			* @brief Copy constructor.
			*
			* @param other The ControlHWND object to copy from.
			*/
			ControlHWND(const ControlHWND&) = default;

			/**
			* @brief Destructor.
			*/
			~ControlHWND();

			/**
			* @brief Assigns a HWND to the ControlHWND object.
			*
			* @param hWnd The HWND to assign.
			* @return ControlHWND& The reference to the ControlHWND object after assignment.
			*/
			ControlHWND& operator=(HWND hWnd);

			/**
			* @brief Move assignment operator.
			*
			* @param other The ControlHWND object to move.
			* @return ControlHWND& The reference to the ControlHWND object after move.
			*/
			ControlHWND& operator=(ControlHWND&&);

			/**
			* @brief Copy assignment operator.
			*
			* @param other The ControlHWND object to copy.
			* @return ControlHWND& The reference to the ControlHWND object after copy.
			*/
			ControlHWND& operator=(const ControlHWND&);

			/**
			* @brief Creates a ControlHWND object from a HWND.
			*
			* @param hWnd The HWND.
			* @return ControlHWND The created ControlHWND object.
			*/
			static ControlHWND FromHandle(HWND hWnd);

			/**
			* @brief Converts the ControlHWND object to a specified control object.
			*
			* @tparam Ctl The type of the control object.
			* @return Ctl* A pointer to the specified control object if the conversion is successful, nullptr otherwise.
			*/
			template<class Ctl>
			Ctl* ToSpecifiedObject()
			{
				static_assert(
					::std::is_base_of_v<Static, Ctl>
					|| ::std::is_base_of_v<Button, Ctl>
					|| ::std::is_base_of_v<Edit, Ctl>
					|| ::std::is_base_of_v<ListBox, Ctl>
					|| ::std::is_base_of_v<ComboBox, Ctl>
					|| ::std::is_base_of_v<ListView, Ctl>
					|| ::std::is_base_of_v<TreeView, Ctl>
					|| ::std::is_base_of_v<ProgressBar, Ctl>
					|| ::std::is_base_of_v<ScrollBar, Ctl>
					|| ::std::is_base_of_v<Tooltips, Ctl>,
					"Invalid template parameter. The template parameter must be (a class derived from) one of the following classes: Static, Button, Edit, ListBox, ComboBox, ListView, TreeView, ProgressBar, ScrollBar.");

				if (Static::FromControlHWND(*this)
					|| Button::FromControlHWND(*this)
					|| Edit::FromControlHWND(*this)
					|| ListBox::FromControlHWND(*this)
					|| ComboBox::FromControlHWND(*this)
					|| ListView::FromControlHWND(*this)
					|| TreeView::FromControlHWND(*this)
					|| ProgressBar::FromControlHWND(*this)
					|| ScrollBar::FromControlHWND(*this)
					|| Tooltips::FromControlHWND(*this))
					return Ctl::FromControlHWND(*this);
				return nullptr;
			}

			/**
			* @brief Represents a fast pointer to a UI object.
			*/
			union
			{
				Static* Static; /**< Pointer to a Static object. */
				Button* Button; /**< Pointer to a Button object. */
				Edit* Edit; /**< Pointer to an Edit object. */
				ListBox* ListBox; /**< Pointer to a ListBox object. */
				ComboBox* ComboBox; /**< Pointer to a ComboBox object. */
				ListView* ListView; /**< Pointer to a ListView object. */
				TreeView* TreeView; /**< Pointer to a TreeView object. */
				ProgressBar* ProgressBar; /**< Pointer to a ProgressBar object. */
				ScrollBar* ScrollBar; /**< Pointer to a ScrollBar object. */
				Tooltips* Tooltips; /**< Pointer to a Tooltips object. */
			}
			/**
			* @brief Constructs a FastPtr object.
			*
			* @return FastPtr The constructed FastPtr object.
			*/
			FastPtr()
			{
				return { reinterpret_cast<Static*>(this) };
			}
		};
	}
}


#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_AMD64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_ARM64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='arm64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#endif // !LOURDLE_UIFRAMEWORK_H
