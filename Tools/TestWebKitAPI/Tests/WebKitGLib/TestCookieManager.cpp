/*
 * Copyright (C) 2012 Igalia S.L.
 * Copyright (C) 2017 Endless Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "WebKitTestServer.h"
#include "WebViewTest.h"
#include <WebCore/GUniquePtrSoup.h>
#include <WebCore/SoupVersioning.h>
#include <glib/gstdio.h>

static WebKitTestServer* kServer;

static const char* kFirstPartyDomain = "127.0.0.1";
static const char* kThirdPartyDomain = "localhost";

static const char* kCookieName = "foo";
static const char* kCookieValue = "bar";
static const char* kCookiePath = "/";

static const char* kCookiePathNew = "/new";
static const char* kCookieValueNew = "new-value";

static const char* kIndexHtmlFormat =
    "<html><body>"
    " <p>WebKitGTK Cookie Manager test</p>"
    " <img src='http://localhost:%u/image.png' width=5 height=5></img>"
    "</body></html>";

class CookieManagerTest: public WebViewTest {
public:
    MAKE_GLIB_TEST_FIXTURE(CookieManagerTest);

    static void cookiesChangedCallback(WebKitCookieManager*, CookieManagerTest* test)
    {
        test->m_cookiesChanged = true;
        if (!(--test->m_cookiesExpectedToChangeCount) && test->m_finishLoopWhenCookiesChange)
            g_main_loop_quit(test->m_mainLoop);
    }

    CookieManagerTest()
        : WebViewTest()
#if ENABLE(2022_GLIB_API)
        , m_cookieManager(webkit_network_session_get_cookie_manager(m_networkSession.get()))
        , m_websiteDataManager(webkit_network_session_get_website_data_manager(m_networkSession.get()))
#else
        , m_cookieManager(webkit_web_context_get_cookie_manager(m_webContext.get()))
        , m_websiteDataManager(webkit_web_context_get_website_data_manager(m_webContext.get()))
#endif
    {
#if !ENABLE(2022_GLIB_API)
        g_assert_true(webkit_website_data_manager_get_cookie_manager(m_websiteDataManager) == m_cookieManager);
#endif
        g_signal_connect(m_cookieManager, "changed", G_CALLBACK(cookiesChangedCallback), this);
    }

    ~CookieManagerTest()
    {
        g_strfreev(m_domains);
        g_list_free_full(m_cookies, reinterpret_cast<GDestroyNotify>(soup_cookie_free));

        g_signal_handlers_disconnect_matched(m_cookieManager, G_SIGNAL_MATCH_DATA, 0, 0, 0, 0, this);
        if (m_cookiesTextFile)
            g_unlink(m_cookiesTextFile.get());
        if (m_cookiesSQLiteFile)
            g_unlink(m_cookiesSQLiteFile.get());
    }

    void setPersistentStorage(WebKitCookiePersistentStorage storage)
    {
        const char* filename = 0;
        switch (storage) {
        case WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT:
            if (!m_cookiesTextFile)
                m_cookiesTextFile.reset(g_build_filename(Test::dataDirectory(), "cookies.txt", nullptr));
            filename = m_cookiesTextFile.get();
            break;
        case WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE:
            if (!m_cookiesSQLiteFile)
                m_cookiesSQLiteFile.reset(g_build_filename(Test::dataDirectory(), "cookies.db", nullptr));
            filename = m_cookiesSQLiteFile.get();
            break;
        default:
            g_assert_not_reached();
        }
        webkit_cookie_manager_set_persistent_storage(m_cookieManager, filename, storage);
    }

    static void getAcceptPolicyReadyCallback(GObject* object, GAsyncResult* result, gpointer userData)
    {
        GUniqueOutPtr<GError> error;
        WebKitCookieAcceptPolicy policy = webkit_cookie_manager_get_accept_policy_finish(WEBKIT_COOKIE_MANAGER(object), result, &error.outPtr());
        g_assert_no_error(error.get());

        CookieManagerTest* test = static_cast<CookieManagerTest*>(userData);
        test->m_acceptPolicy = policy;
        g_main_loop_quit(test->m_mainLoop);
    }

    WebKitCookieAcceptPolicy getAcceptPolicy()
    {
        m_acceptPolicy = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
        webkit_cookie_manager_get_accept_policy(m_cookieManager, 0, getAcceptPolicyReadyCallback, this);
        g_main_loop_run(m_mainLoop);

        return m_acceptPolicy;
    }

    static void addCookieReadyCallback(GObject* object, GAsyncResult* result, gpointer userData)
    {
        GUniqueOutPtr<GError> error;
        bool added = webkit_cookie_manager_add_cookie_finish(WEBKIT_COOKIE_MANAGER(object), result, &error.outPtr());
        g_assert_no_error(error.get());
        g_assert_true(added);

        CookieManagerTest* test = static_cast<CookieManagerTest*>(userData);
        g_main_loop_quit(test->m_mainLoop);
    }

    void addCookie(SoupCookie* cookie)
    {
        webkit_cookie_manager_add_cookie(m_cookieManager, cookie, 0, addCookieReadyCallback, this);
        g_main_loop_run(m_mainLoop);
    }

    static void getCookiesReadyCallback(GObject* object, GAsyncResult* result, gpointer userData)
    {
        GUniqueOutPtr<GError> error;
        GList* cookies = webkit_cookie_manager_get_cookies_finish(WEBKIT_COOKIE_MANAGER(object), result, &error.outPtr());
        g_assert_no_error(error.get());

        CookieManagerTest* test = static_cast<CookieManagerTest*>(userData);
        test->m_cookies = cookies;
        g_main_loop_quit(test->m_mainLoop);
    }

    GList* getCookies(const char* uri)
    {
        g_list_free_full(m_cookies, reinterpret_cast<GDestroyNotify>(soup_cookie_free));
        m_cookies = nullptr;
        webkit_cookie_manager_get_cookies(m_cookieManager, uri, 0, getCookiesReadyCallback, this);
        g_main_loop_run(m_mainLoop);

        return m_cookies;
    }

    static void replaceCookiesReadyCallback(GObject* object, GAsyncResult* result, gpointer userData)
    {
        CookieManagerTest* test = static_cast<CookieManagerTest*>(userData);
        GUniqueOutPtr<GError> error;
        webkit_cookie_manager_replace_cookies_finish(WEBKIT_COOKIE_MANAGER(object), result, &error.outPtr());
        g_assert_no_error(error.get());

        g_main_loop_quit(test->m_mainLoop);
    }

    void replaceCookies(GList* cookies)
    {
        webkit_cookie_manager_replace_cookies(m_cookieManager, cookies, 0, replaceCookiesReadyCallback, this);
        g_main_loop_run(m_mainLoop);

    }

    static void getAllCookiesReadyCallback(GObject* object, GAsyncResult* result, gpointer userData)
    {
        GUniqueOutPtr<GError> error;
        GList* cookies = webkit_cookie_manager_get_all_cookies_finish(WEBKIT_COOKIE_MANAGER(object), result, &error.outPtr());
        g_assert_no_error(error.get());

        CookieManagerTest* test = static_cast<CookieManagerTest*>(userData);
        test->m_cookies = cookies;
        g_main_loop_quit(test->m_mainLoop);
    }

    GList* getAllCookies()
    {
        g_list_free_full(m_cookies, reinterpret_cast<GDestroyNotify>(soup_cookie_free));
        m_cookies = nullptr;
        webkit_cookie_manager_get_all_cookies(m_cookieManager, 0, getAllCookiesReadyCallback, this);
        g_main_loop_run(m_mainLoop);

        return m_cookies;
    }

    static void deleteCookieReadyCallback(GObject* object, GAsyncResult* result, gpointer userData)
    {
        GUniqueOutPtr<GError> error;
        bool deleted = webkit_cookie_manager_delete_cookie_finish(WEBKIT_COOKIE_MANAGER(object), result, &error.outPtr());
        g_assert_no_error(error.get());
        g_assert_true(deleted);

        CookieManagerTest* test = static_cast<CookieManagerTest*>(userData);
        g_main_loop_quit(test->m_mainLoop);
    }

    void deleteCookie(SoupCookie* cookie)
    {
        webkit_cookie_manager_delete_cookie(m_cookieManager, cookie, 0, deleteCookieReadyCallback, this);
        g_main_loop_run(m_mainLoop);
    }

    void setAcceptPolicy(WebKitCookieAcceptPolicy policy)
    {
        webkit_cookie_manager_set_accept_policy(m_cookieManager, policy);
    }

    GUniquePtr<GList> fetch()
    {
        struct TestData {
            GUniquePtr<GList> dataList;
            GMainLoop* mainLoop;
        } data = { nullptr, m_mainLoop };

        webkit_website_data_manager_fetch(m_websiteDataManager, WEBKIT_WEBSITE_DATA_COOKIES, nullptr, [](GObject* object, GAsyncResult* result, gpointer userData) {
            auto* data = static_cast<TestData*>(userData);
            GUniqueOutPtr<GError> error;
            data->dataList.reset(webkit_website_data_manager_fetch_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, &error.outPtr()));
            g_assert_no_error(error.get());
            g_main_loop_quit(data->mainLoop);
        }, &data);
        g_main_loop_run(m_mainLoop);
        return WTFMove(data.dataList);
    }

    char** getDomains()
    {
        g_clear_pointer(&m_domains, g_strfreev);
        GUniquePtr<GList> dataList = fetch();
        GPtrArray* domains = g_ptr_array_sized_new(g_list_length(dataList.get()));
        for (GList* item = dataList.get(); item; item = g_list_next(item)) {
            auto* data = static_cast<WebKitWebsiteData*>(item->data);
            g_ptr_array_add(domains, g_strdup(webkit_website_data_get_name(data)));
            webkit_website_data_unref(data);
        }
        g_ptr_array_add(domains, nullptr);

        m_domains = reinterpret_cast<char**>(g_ptr_array_free(domains, FALSE));
        return m_domains;
    }

    bool hasDomain(const char* domain)
    {
        if (!m_domains)
            return false;

        for (size_t i = 0; m_domains[i]; ++i) {
            if (g_str_equal(m_domains[i], domain))
                return true;
        }
        return false;
    }

    void deleteCookiesForDomain(const char* domain)
    {
        GUniquePtr<GList> dataList = fetch();
        GUniquePtr<GList> cookies;
        for (GList* item = dataList.get(); item; item = g_list_next(item)) {
            auto* data = static_cast<WebKitWebsiteData*>(item->data);
            if (!g_strcmp0(webkit_website_data_get_name(data), domain))
                cookies.reset(g_list_prepend(cookies.get(), webkit_website_data_ref(data)));
            webkit_website_data_unref(data);
        }
        webkit_website_data_manager_remove(m_websiteDataManager, WEBKIT_WEBSITE_DATA_COOKIES, cookies.get(), nullptr, nullptr, nullptr);
        g_list_foreach(cookies.get(), [](gpointer data, gpointer) {
            webkit_website_data_unref(static_cast<WebKitWebsiteData*>(data));
        }, nullptr);
    }

    void deleteAllCookies()
    {
        webkit_website_data_manager_clear(m_websiteDataManager, WEBKIT_WEBSITE_DATA_COOKIES, 0, nullptr, nullptr, nullptr);
    }

    void waitUntilCookiesChanged(int cookiesExpectedToChangeCount = 1)
    {
        m_cookiesChanged = false;
        m_cookiesExpectedToChangeCount = cookiesExpectedToChangeCount;
        m_finishLoopWhenCookiesChange = true;
        g_main_loop_run(m_mainLoop);
        m_finishLoopWhenCookiesChange = false;
    }

    void setITPEnable(bool enable)
    {
#if ENABLE(2022_GLIB_API)
        webkit_network_session_set_itp_enabled(m_networkSession.get(), enable);
#else
        webkit_website_data_manager_set_itp_enabled(m_websiteDataManager, enable);
#endif
    }

    WebKitCookieManager* m_cookieManager { nullptr };
    WebKitWebsiteDataManager* m_websiteDataManager { nullptr };
    WebKitCookieAcceptPolicy m_acceptPolicy { WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY };
    char** m_domains { nullptr };
    GList* m_cookies { nullptr };
    bool m_cookiesChanged { false };
    int m_cookiesExpectedToChangeCount { 0 };
    bool m_finishLoopWhenCookiesChange { false };
    GUniquePtr<char> m_cookiesTextFile;
    GUniquePtr<char> m_cookiesSQLiteFile;
};

static void testCookieManagerAcceptPolicy(CookieManagerTest* test, gconstpointer)
{
    // Default policy is NO_THIRD_PARTY.
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    char** domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 1);
    g_assert_cmpstr(domains[0], ==, kFirstPartyDomain);
    test->deleteAllCookies();

    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 2);
    g_assert_true(test->hasDomain(kFirstPartyDomain));
    g_assert_true(test->hasDomain(kThirdPartyDomain));
    test->deleteAllCookies();

    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 0);

    // ITP never uses NO_THIRD_PARTY.
    test->setITPEnable(true);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->setITPEnable(false);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
    test->setITPEnable(true);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->setITPEnable(false);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->setITPEnable(true);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
    test->setITPEnable(false);
    g_assert_cmpint(test->getAcceptPolicy(), ==, WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
}

static void testCookieManagerAddCookie(CookieManagerTest* test, gconstpointer)
{
    // Load the html content, with the default NO_THIRD_PARTY accept policy,
    // which will automatically add one cookie.
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);

    // Check the cookies that have been added for the domain.
    GUniquePtr<char> uri(g_strdup_printf("http://%s", kFirstPartyDomain));
    GList* foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    SoupCookie* foundCookie = static_cast<SoupCookie*>(foundCookies->data);
    g_assert_cmpstr(soup_cookie_get_name(foundCookie), ==, kCookieName);
    g_assert_cmpstr(soup_cookie_get_domain(foundCookie), ==, kFirstPartyDomain);
    g_assert_cmpstr(soup_cookie_get_path(foundCookie), ==, kCookiePath);
    g_assert_cmpstr(soup_cookie_get_value(foundCookie), ==, kCookieValue);

    // Try to add now a cookie with same (name, domain, path) than the ones already added.
    GUniquePtr<SoupCookie> firstCookie(soup_cookie_new(kCookieName, kCookieValueNew, kFirstPartyDomain, kCookiePath, SOUP_COOKIE_MAX_AGE_ONE_HOUR));
    test->addCookie(firstCookie.get());

    // Still one cookie, since (name, domain, path) are the same than the already existing
    // one, but the new value is now stored as replaced by the recently added cookie.
    foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    foundCookie = static_cast<SoupCookie*>(foundCookies->data);
    g_assert_cmpstr(soup_cookie_get_name(foundCookie), ==, kCookieName);
    g_assert_cmpstr(soup_cookie_get_domain(foundCookie), ==, kFirstPartyDomain);
    g_assert_cmpstr(soup_cookie_get_path(foundCookie), ==, kCookiePath);
    g_assert_cmpstr(soup_cookie_get_value(foundCookie), ==, kCookieValueNew);

    // Now create another cookie with a different path and add it.
    GUniquePtr<SoupCookie> secondCookie(soup_cookie_new(kCookieName, kCookieValueNew, kFirstPartyDomain, kCookiePathNew, SOUP_COOKIE_MAX_AGE_ONE_HOUR));
    test->addCookie(secondCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);

    // Retrieve the list of cookies for the same domain and path again now and check.
    uri.reset(g_strdup_printf("http://%s%s", kFirstPartyDomain, kCookiePathNew));
    foundCookies = test->getCookies(uri.get());

    // We have now two cookies that would apply to the passed URL, one is the cookie initially
    // loaded with the web content and the other cookie the one we manually added.
    g_assert_cmpint(g_list_length(foundCookies), ==, 2);

    // Add a third new cookie for a different domain than the previous ones.
    GUniquePtr<SoupCookie> thirdCookie(soup_cookie_new(kCookieName, kCookieValueNew, kThirdPartyDomain, kCookiePathNew, SOUP_COOKIE_MAX_AGE_ONE_HOUR));
    test->addCookie(thirdCookie.get());

    // Only one cookie now, since the domain is different.
    uri.reset(g_strdup_printf("http://%s%s", kThirdPartyDomain, kCookiePathNew));
    foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 2);

    foundCookie = static_cast<SoupCookie*>(foundCookies->data);
    g_assert_cmpstr(soup_cookie_get_name(foundCookie), ==, kCookieName);
    g_assert_cmpstr(soup_cookie_get_domain(foundCookie), ==, kThirdPartyDomain);
    g_assert_cmpstr(soup_cookie_get_path(foundCookie), ==, kCookiePathNew);
    g_assert_cmpstr(soup_cookie_get_value(foundCookie), ==, kCookieValueNew);

    // Finally, delete all cookies and check they are all gone.
    test->deleteAllCookies();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);
}

static void testCookieManagerGetCookies(CookieManagerTest* test, gconstpointer)
{
    // Load the html content and retrieve the two cookies automatically added with ALWAYS policy.
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 2);

    // Retrieve the first cookie using a HTTP scheme.
    GUniquePtr<char> uri(g_strdup_printf("http://%s", kFirstPartyDomain));
    GList* foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    SoupCookie* foundCookie = static_cast<SoupCookie*>(foundCookies->data);
    g_assert_cmpstr(soup_cookie_get_name(foundCookie), ==, kCookieName);
    g_assert_cmpstr(soup_cookie_get_domain(foundCookie), ==, kFirstPartyDomain);
    g_assert_cmpstr(soup_cookie_get_path(foundCookie), ==, kCookiePath);
    g_assert_cmpstr(soup_cookie_get_value(foundCookie), ==, kCookieValue);

    // Retrieve the second cookie using a HTTPS scheme.
    uri.reset(g_strdup_printf("https://%s", kThirdPartyDomain));
    foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    foundCookie = static_cast<SoupCookie*>(foundCookies->data);
    g_assert_cmpstr(soup_cookie_get_name(foundCookie), ==, kCookieName);
    g_assert_cmpstr(soup_cookie_get_domain(foundCookie), ==, kThirdPartyDomain);
    g_assert_cmpstr(soup_cookie_get_path(foundCookie), ==, kCookiePath);
    g_assert_cmpstr(soup_cookie_get_value(foundCookie), ==, kCookieValue);

    // Create a new cookie and add it to the first domain.
    GUniquePtr<SoupCookie> newCookie(soup_cookie_new(kCookieName, kCookieValueNew, kFirstPartyDomain, kCookiePathNew, SOUP_COOKIE_MAX_AGE_ONE_HOUR));
    test->addCookie(newCookie.get());

    // We should get two cookies that would apply to the same URL passed, since
    // http://127.0.0.1/new is a subset of the http://127.0.0.1/ URL.
    uri.reset(g_strdup_printf("http://%s%s", kFirstPartyDomain, kCookiePathNew));
    foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 2);

    // We have now two cookies that would apply to the passed URL, one is the cookie initially
    // loaded with the web content and the other cookie the one we manually added.
    g_assert_cmpint(g_list_length(foundCookies), ==, 2);

    bool newPathChecked = false;
    const char* pathFound = nullptr;
    const char* valueFound = nullptr;
    for (uint i = 0; i < 2; i++) {
        foundCookie = static_cast<SoupCookie*>(g_list_nth_data(foundCookies, i));
        g_assert_cmpstr(soup_cookie_get_name(foundCookie), ==, kCookieName);
        g_assert_cmpstr(soup_cookie_get_domain(foundCookie), ==, kFirstPartyDomain);

        // Cookies will have different values for 'value' and 'path', so make sure that
        // we check for both possibilities, but different ones for each cookie found.
        pathFound = soup_cookie_get_path(foundCookie);
        valueFound = soup_cookie_get_value(foundCookie);
        if (i > 0) {
            if (newPathChecked) {
                g_assert_cmpstr(pathFound, ==, kCookiePath);
                g_assert_cmpstr(valueFound, ==, kCookieValue);
            } else {
                g_assert_cmpstr(pathFound, ==, kCookiePathNew);
                g_assert_cmpstr(valueFound, ==, kCookieValueNew);
            }
        } else {
            if (g_strcmp0(pathFound, kCookiePath)) {
                g_assert_cmpstr(pathFound, ==, kCookiePathNew);
                g_assert_cmpstr(valueFound, ==, kCookieValueNew);
                newPathChecked = true;
            }

            if (g_strcmp0(pathFound, kCookiePathNew)) {
                g_assert_cmpstr(pathFound, ==, kCookiePath);
                g_assert_cmpstr(valueFound, ==, kCookieValue);
                newPathChecked = false;
            }
        }
    }

    // We should get 1 cookie only if we specify http://127.0.0.1/, though.
    uri.reset(g_strdup_printf("http://%s", kFirstPartyDomain));
    foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    foundCookie = static_cast<SoupCookie*>(foundCookies->data);
    g_assert_cmpstr(soup_cookie_get_name(foundCookie), ==, kCookieName);
    g_assert_cmpstr(soup_cookie_get_domain(foundCookie), ==, kFirstPartyDomain);
    g_assert_cmpstr(soup_cookie_get_path(foundCookie), ==, kCookiePath);
    g_assert_cmpstr(soup_cookie_get_value(foundCookie), ==, kCookieValue);

    // Finally, delete all cookies and try to retrieve them again, one by one.
    test->deleteAllCookies();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);

    uri.reset(g_strdup_printf("http://%s", kFirstPartyDomain));
    foundCookies = test->getCookies(uri.get());
    g_assert_null(foundCookies);
}

static void testCookieManagerReplaceGetAllCookies(CookieManagerTest* test, gconstpointer)
{
    // Load the html content and retrieve the two cookies automatically added with ALWAYS policy.
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 2);

    // Retrieve existing cookie jar.
    GList* foundCookies = test->getAllCookies();
    g_assert_cmpint(g_list_length(foundCookies), ==, 2);

    GUniquePtr<SoupCookie> firstCookie(soup_cookie_new(kCookieName, kCookieValueNew, kFirstPartyDomain, kCookiePath, SOUP_COOKIE_MAX_AGE_ONE_HOUR));
    GUniquePtr<GList> cookies;
    cookies.reset(g_list_prepend(cookies.get(), firstCookie.get()));

    // Only one signal "changed" should be sent in case of setCookieJar.
    test->m_cookiesChanged = false;
    test->m_cookiesExpectedToChangeCount = 1;
    // Set new cookie jar with one cookie.
    test->replaceCookies(cookies.get());
    g_assert_true(test->m_cookiesChanged);
    g_assert_cmpint(test->m_cookiesExpectedToChangeCount, ==, 0);

    // Retrieve the new cookie jar with one cookie.
    foundCookies = test->getAllCookies();
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    // Finally, delete all cookies.
    test->deleteAllCookies();

    // Retrieve the empty cookie jar.
    foundCookies = test->getAllCookies();
    g_assert_null(foundCookies);
}

static void testCookieManagerDeleteCookie(CookieManagerTest* test, gconstpointer)
{
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();

    // Initially, there should be two cookies available.
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 2);

    // Delete the cookie for the first party domain.
    GUniquePtr<char> uri(g_strdup_printf("http://%s", kFirstPartyDomain));
    GList* foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    GUniquePtr<SoupCookie> firstPartyCookie(soup_cookie_copy(static_cast<SoupCookie*>(foundCookies->data)));
    test->deleteCookie(firstPartyCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);

    // Try deleting a non-existent cookie (wrong name).
    GUniquePtr<SoupCookie> wrongCookie(soup_cookie_new("wrong-name", kCookieValue, kThirdPartyDomain, kCookiePath, -1));
    test->deleteCookie(wrongCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);

    // Try deleting a non-existent cookie (wrong domain).
    wrongCookie.reset(soup_cookie_new(kCookieName, kCookieValue, "wrong-domain", kCookiePath, -1));
    test->deleteCookie(wrongCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);

    // Try deleting a non-existent cookie (wrong path).
    wrongCookie.reset(soup_cookie_new(kCookieName, kCookieValue, kThirdPartyDomain, "wrong-path", -1));
    test->deleteCookie(wrongCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);

    // Delete the cookie for the third party domain.
    uri.reset(g_strdup_printf("http://%s", kThirdPartyDomain));
    foundCookies = test->getCookies(uri.get());
    g_assert_cmpint(g_list_length(foundCookies), ==, 1);

    GUniquePtr<SoupCookie> thirdPartyCookie(soup_cookie_copy(static_cast<SoupCookie*>(foundCookies->data)));
    test->deleteCookie(thirdPartyCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);

    // Finally, add a new cookie now we don't have any and delete it afterwards.
    GUniquePtr<SoupCookie> newCookie(soup_cookie_new(kCookieName, kCookieValueNew, kFirstPartyDomain, kCookiePathNew, SOUP_COOKIE_MAX_AGE_ONE_HOUR));
    test->addCookie(newCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);
    test->deleteCookie(newCookie.get());
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);
}

static void testCookieManagerDeleteCookies(CookieManagerTest* test, gconstpointer)
{
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 2);

    // Delete first party cookies.
    test->deleteCookiesForDomain(kFirstPartyDomain);
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 1);

    // Delete third party cookies.
    test->deleteCookiesForDomain(kThirdPartyDomain);
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);

    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 2);

    // Delete all cookies.
    test->deleteAllCookies();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);
}

static void testCookieManagerCookiesChanged(CookieManagerTest* test, gconstpointer)
{
    g_assert_false(test->m_cookiesChanged);
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_true(test->m_cookiesChanged);

    test->deleteCookiesForDomain(kFirstPartyDomain);
    test->waitUntilCookiesChanged();
    g_assert_true(test->m_cookiesChanged);

    test->deleteAllCookies();
    test->waitUntilCookiesChanged();
    g_assert_true(test->m_cookiesChanged);
}

class CookiePersistentStorageTest : public CookieManagerTest {
public:
    MAKE_GLIB_TEST_FIXTURE_WITH_SETUP_TEARDOWN(CookiePersistentStorageTest, setup, teardown);

    static void setup()
    {
        WebViewTest::shouldInitializeWebViewInConstructor = false;
    }

    static void teardown()
    {
        WebViewTest::shouldInitializeWebViewInConstructor = true;
    }
};

static void testCookieManagerPersistentStorage(CookiePersistentStorageTest* test, gconstpointer)
{
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

    g_unlink(test->m_cookiesTextFile.get());
    g_unlink(test->m_cookiesSQLiteFile.get());

    // Text storage using a new file.
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
    char** domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 0);

    // Initialization of web view is deferred to ensure it's not required for
    // setting persistent storage to work.
    test->initializeWebView();
    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_true(test->m_cookiesChanged);
    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 2);
    g_assert_true(g_file_test(test->m_cookiesTextFile.get(), G_FILE_TEST_EXISTS));

    // SQLite storage using a new file.
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 0);

    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();
    g_assert_true(test->m_cookiesChanged);
    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 2);
    g_assert_true(g_file_test(test->m_cookiesSQLiteFile.get(), G_FILE_TEST_EXISTS));

    // Text storage using an existing file.
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 2);
    test->deleteAllCookies();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);

    // SQLite storage with an existing file.
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 2);
    test->deleteAllCookies();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);
}

static void testCookieManagerPersistentStorageDeleteAll(CookieManagerTest* test, gconstpointer)
{
    // This checks that we can remove all the cookies of an existing file before a web process is created.
    // See bug https://bugs.webkit.org/show_bug.cgi?id=175265.
    static const char cookiesFileFormat[] = "127.0.0.1\tFALSE\t/\tFALSE\t%ld\tfoo\tbar\nlocalhost\tFALSE\t/\tFALSE\t%ld\tbaz\tqux\n";
    time_t expires = time(nullptr) + 60;
    GUniquePtr<char> cookiesFileContents(g_strdup_printf(cookiesFileFormat, expires, expires));
    GUniquePtr<char> cookiesFile(g_build_filename(Test::dataDirectory(), "cookies.txt", nullptr));
    g_assert_true(g_file_set_contents(cookiesFile.get(), cookiesFileContents.get(), -1, nullptr));

    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
    test->deleteAllCookies();
    // Changed signal is emitted for every deleted cookie, twice in this case.
    test->waitUntilCookiesChanged(2);

    // Ensure the web process is created and load something without cookies.
    test->m_cookiesChanged = false;
    test->loadURI(kServer->getURIForPath("/no-cookies.html").data());
    test->waitUntilLoadFinished();
    g_assert_false(test->m_cookiesChanged);
    char** domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 0);
}

static void ephemeralViewloadChanged(WebKitWebView* webView, WebKitLoadEvent loadEvent, WebViewTest* test)
{
    if (loadEvent != WEBKIT_LOAD_FINISHED)
        return;
    g_signal_handlers_disconnect_by_func(webView, reinterpret_cast<void*>(ephemeralViewloadChanged), test);
    test->quitMainLoop();
}

static void testCookieManagerEphemeral(CookieManagerTest* test, gconstpointer)
{
    test->setAcceptPolicy(WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
    char** domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 0);

#if ENABLE(2022_GLIB_API)
    GRefPtr<WebKitNetworkSession> ephemeralSession = adoptGRef(webkit_network_session_new_ephemeral());
#endif
    auto webView = test->createWebView(
#if ENABLE(2022_GLIB_API)
        "network-session", ephemeralSession.get(),
#else
        "is-ephemeral", TRUE,
#endif
        nullptr);
#if ENABLE(2022_GLIB_API)
    g_assert_true(webkit_web_view_get_network_session(webView.get()) == ephemeralSession.get());
#else
    g_assert_true(webkit_web_view_is_ephemeral(webView.get()));
    g_assert_false(webkit_web_context_is_ephemeral(webkit_web_view_get_context(webView.get())));
#endif

    g_signal_connect(webView.get(), "load-changed", G_CALLBACK(ephemeralViewloadChanged), test);
    webkit_web_view_load_uri(webView.get(), kServer->getURIForPath("/index.html").data());
    g_main_loop_run(test->m_mainLoop);

    domains = test->getDomains();
    g_assert_nonnull(domains);
    g_assert_cmpint(g_strv_length(domains), ==, 0);

#if ENABLE(2022_GLIB_API)
    auto* viewDataManager = webkit_network_session_get_website_data_manager(ephemeralSession.get());
#else
    auto* viewDataManager = webkit_web_view_get_website_data_manager(webView.get());
#endif
    g_assert_true(WEBKIT_IS_WEBSITE_DATA_MANAGER(viewDataManager));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(viewDataManager));
#if ENABLE(2022_GLIB_API)
    auto* cookieManager = webkit_network_session_get_cookie_manager(ephemeralSession.get());
#else
    g_assert_true(viewDataManager != webkit_web_context_get_website_data_manager(webkit_web_view_get_context(test->webView())));
    auto* cookieManager = webkit_website_data_manager_get_cookie_manager(viewDataManager);
#endif
    g_assert_true(WEBKIT_IS_COOKIE_MANAGER(cookieManager));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(cookieManager));
    g_assert_true(cookieManager != test->m_cookieManager);
    webkit_website_data_manager_fetch(viewDataManager, WEBKIT_WEBSITE_DATA_COOKIES, nullptr, [](GObject* object, GAsyncResult* result, gpointer userData) {
        auto* test = static_cast<CookieManagerTest*>(userData);
        GUniquePtr<GList> domains(webkit_website_data_manager_fetch_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, nullptr));
        g_assert_nonnull(domains);
        g_assert_cmpint(g_list_length(domains.get()), ==, 1);
        auto* data = static_cast<WebKitWebsiteData*>(domains.get()->data);
        g_assert_cmpstr(webkit_website_data_get_name(data), ==, kFirstPartyDomain);
        webkit_website_data_unref(data);
        test->quitMainLoop();
    }, test);
    g_main_loop_run(test->m_mainLoop);
}

static void testCookieManagerLongExpires(CookieManagerTest* test, gconstpointer)
{
    g_unlink(test->m_cookiesTextFile.get());
    g_unlink(test->m_cookiesSQLiteFile.get());

    GRefPtr<GDateTime> now = adoptGRef(g_date_time_new_now_utc());
    GRefPtr<GDateTime> expires = adoptGRef(g_date_time_add_years(now.get(), 35));
    GUniquePtr<char> line(g_strdup_printf("#HttpOnly_localhost\tFALSE\t/\tFALSE\t%ld\tprov\t123", g_date_time_to_unix(expires.get())));
    test->m_cookiesTextFile.reset(g_build_filename(Test::dataDirectory(), "cookies.txt", nullptr));
    g_file_set_contents(test->m_cookiesTextFile.get(), line.get(), -1, nullptr);
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);

    GList* cookies = test->getCookies("http://localhost/");
    g_assert_cmpint(g_list_length(cookies), ==, 1);
    SoupCookie* cookie = static_cast<SoupCookie*>(cookies->data);
    auto* cookiesExpires = soup_cookie_get_expires(cookie);
    g_assert_nonnull(cookiesExpires);
#if USE(SOUP2)
    g_assert_cmpint(g_date_time_to_unix(expires.get()), ==, soup_date_to_time_t(cookiesExpires));
#else
    g_assert_cmpint(g_date_time_to_unix(expires.get()), ==, g_date_time_to_unix(cookiesExpires));
#endif

    test->deleteAllCookies();
    g_assert_cmpint(g_strv_length(test->getDomains()), ==, 0);
}

static void testCookieSyncWithWebView(CookiePersistentStorageTest* test, gconstpointer)
{
    g_unlink(test->m_cookiesTextFile.get());
    g_unlink(test->m_cookiesSQLiteFile.get());
    test->m_cookiesTextFile.reset(g_build_filename(Test::dataDirectory(), "cookies.txt", nullptr));

    // When COOKIE_CHANGE_LISTENER_API is defined the WebCookieCache is enabled which requires
    // on every change NetworkStorageSessionSoup sends messages to the WebProcess to keep it updated.
    // So here we are testing webkit_cookie_manager_replace_cookies() and webkit_cookie_manager_set_persistent_storage() removes everything from the cache.
    // Some waits have been added just to be more reliable as there is a lot of IPC (UI -> Network -> WebProcess).

    test->initializeWebView();
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    GUniquePtr<SoupCookie> cookie(soup_cookie_new(kCookieName, kCookieValue, kFirstPartyDomain, kCookiePath, SOUP_COOKIE_MAX_AGE_ONE_DAY));
    test->addCookie(cookie.get());

    test->loadURI(kServer->getURIForPath("/index.html").data());
    test->waitUntilLoadFinished();

    auto* value = test->runJavaScriptAndWaitUntilFinished("document.cookie", nullptr);
    GUniquePtr<char> cookieString(jsc_value_to_string(value));
    g_assert_cmpstr(cookieString.get(), ==, "foo=bar");

    GUniquePtr<SoupCookie> newCookie(soup_cookie_new(kCookieName, kCookieValueNew, kFirstPartyDomain, kCookiePath, SOUP_COOKIE_MAX_AGE_ONE_DAY));
    GUniquePtr<SoupCookie> thirdPartyCookie(soup_cookie_new(kCookiePathNew, kCookieName, kThirdPartyDomain, kCookiePath, SOUP_COOKIE_MAX_AGE_ONE_DAY));
    GUniquePtr<GList> cookies(g_list_append(g_list_append(nullptr, newCookie.get()), thirdPartyCookie.get()));

    bool callbackDone = false;
    webkit_cookie_manager_replace_cookies(test->m_cookieManager, cookies.get(), nullptr, (GAsyncReadyCallback)+[](WebKitCookieManager* manager, GAsyncResult* result, bool* done) {
        g_assert_true(webkit_cookie_manager_replace_cookies_finish(manager, result, nullptr));
        *done = true;
    }, &callbackDone);
    while (!callbackDone)
        g_main_context_iteration(g_main_loop_get_context(test->m_mainLoop), TRUE);

    test->wait(1.0);
    value = test->runJavaScriptAndWaitUntilFinished("document.cookie", nullptr);
    cookieString.reset(jsc_value_to_string(value));
    g_assert_cmpstr(cookieString.get(), ==, "foo=new-value");

    g_assert_true(g_file_set_contents(test->m_cookiesTextFile.get(), "127.0.0.1\tFALSE\t/\tFALSE\t-1\tbaz\tvalue\n", -1, nullptr));
    test->setPersistentStorage(WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);

    test->wait(1.0);
    value = test->runJavaScriptAndWaitUntilFinished("document.cookie", nullptr);
    cookieString.reset(jsc_value_to_string(value));
    g_assert_cmpstr(cookieString.get(), ==, "baz=value");
}

#if USE(SOUP2)
static void serverCallback(SoupServer* server, SoupMessage* message, const char* path, GHashTable*, SoupClientContext*, gpointer)
#else
static void serverCallback(SoupServer* server, SoupServerMessage* message, const char* path, GHashTable*, gpointer)
#endif
{
    if (soup_server_message_get_method(message) != SOUP_METHOD_GET) {
        soup_server_message_set_status(message, SOUP_STATUS_NOT_IMPLEMENTED, nullptr);
        return;
    }

    soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
    gchar* header_str = g_strdup_printf("%s=%s; Max-Age=60", kCookieName, kCookieValue);

    if (g_str_equal(path, "/index.html")) {
        char* indexHtml = g_strdup_printf(kIndexHtmlFormat, kServer->port());
        soup_message_headers_replace(soup_server_message_get_response_headers(message), "Set-Cookie", header_str);
        soup_message_body_append(soup_server_message_get_response_body(message), SOUP_MEMORY_TAKE, indexHtml, strlen(indexHtml));
    } else if (g_str_equal(path, "/image.png"))
        soup_message_headers_replace(soup_server_message_get_response_headers(message), "Set-Cookie", header_str);
    else if (g_str_equal(path, "/no-cookies.html")) {
        static const char* indexHtml = "<html><body><p>No cookies</p></body></html>";
        soup_message_body_append(soup_server_message_get_response_body(message), SOUP_MEMORY_STATIC, indexHtml, strlen(indexHtml));
    } else
        soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, nullptr);
    soup_message_body_complete(soup_server_message_get_response_body(message));
}

void beforeAll()
{
    kServer = new WebKitTestServer();
    kServer->run(serverCallback);

    CookieManagerTest::add("WebKitCookieManager", "accept-policy", testCookieManagerAcceptPolicy);
    CookieManagerTest::add("WebKitCookieManager", "add-cookie", testCookieManagerAddCookie);
    CookieManagerTest::add("WebKitCookieManager", "get-cookies", testCookieManagerGetCookies);
    CookieManagerTest::add("WebKitCookieManager", "replace-get-all-cookies", testCookieManagerReplaceGetAllCookies);
    CookieManagerTest::add("WebKitCookieManager", "delete-cookie", testCookieManagerDeleteCookie);
    CookieManagerTest::add("WebKitCookieManager", "delete-cookies", testCookieManagerDeleteCookies);
    CookieManagerTest::add("WebKitCookieManager", "cookies-changed", testCookieManagerCookiesChanged);
    CookiePersistentStorageTest::add("WebKitCookieManager", "persistent-storage", testCookieManagerPersistentStorage);
    CookieManagerTest::add("WebKitCookieManager", "persistent-storage-delete-all", testCookieManagerPersistentStorageDeleteAll);
    CookieManagerTest::add("WebKitCookieManager", "ephemeral", testCookieManagerEphemeral);
    CookieManagerTest::add("WebKitCookieManager", "long-expires", testCookieManagerLongExpires);
    CookiePersistentStorageTest::add("WebKitCookieManager", "sync-with-webview", testCookieSyncWithWebView);
}

void afterAll()
{
    delete kServer;
}
