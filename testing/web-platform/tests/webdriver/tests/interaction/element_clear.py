import pytest

from tests.support.asserts import assert_error, assert_success
from tests.support.inline import inline


@pytest.fixture(scope="session")
def text_file(tmpdir_factory):
    fh = tmpdir_factory.mktemp("tmp").join("hello.txt")
    fh.write("hello")
    return fh


def element_clear(session, element):
    return session.transport.send("POST", "/session/%s/element/%s/clear" %
                                  (session.session_id, element.id))


def test_closed_context(session, create_window):
    new_window = create_window()
    session.window_handle = new_window
    session.url = inline("<input>")
    element = session.find.css("input", all=False)
    session.close()

    response = element_clear(session, element)
    assert_error(response, "no such window")


def test_connected_element(session):
    session.url = inline("<input>")
    element = session.find.css("input", all=False)

    session.url = inline("<input>")
    response = element_clear(session, element)
    assert_error(response, "stale element reference")


def test_pointer_interactable(session):
    session.url = inline("<input style='margin-left: -1000px' value=foobar>")
    element = session.find.css("input", all=False)

    response = element_clear(session, element)
    assert_error(response, "element not interactable")


def test_keyboard_interactable(session):
    session.url = inline("""
        <input value=foobar>
        <div></div>

        <style>
        div {
          position: absolute;
          background: blue;
          top: 0;
        }
        </style>
        """)
    element = session.find.css("input", all=False)
    assert element.property("value") == "foobar"

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == ""


@pytest.mark.parametrize("type,value,default",
                         [("number", "42", ""),
                          ("range", "42", "50"),
                          ("email", "foo@example.com", ""),
                          ("password", "password", ""),
                          ("search", "search", ""),
                          ("tel", "999", ""),
                          ("text", "text", ""),
                          ("url", "https://example.com/", ""),
                          ("color", "#ff0000", "#000000"),
                          ("date", "2017-12-26", ""),
                          ("datetime", "2017-12-26T19:48", ""),
                          ("datetime-local", "2017-12-26T19:48", ""),
                          ("time", "19:48", ""),
                          ("month", "2017-11", ""),
                          ("week", "2017-W52", "")])
def test_input(session, type, value, default):
    session.url = inline("<input type=%s value='%s'>" % (type, value))
    element = session.find.css("input", all=False)
    assert element.property("value") == value

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == default


@pytest.mark.parametrize("type",
                         ["number",
                          "range",
                          "email",
                          "password",
                          "search",
                          "tel",
                          "text",
                          "url",
                          "color",
                          "date",
                          "datetime",
                          "datetime-local",
                          "time",
                          "month",
                          "week",
                          "file"])
def test_input_disabled(session, type):
    session.url = inline("<input type=%s disabled>" % type)
    element = session.find.css("input", all=False)

    response = element_clear(session, element)
    assert_error(response, "invalid element state")


@pytest.mark.parametrize("type",
                         ["number",
                          "range",
                          "email",
                          "password",
                          "search",
                          "tel",
                          "text",
                          "url",
                          "color",
                          "date",
                          "datetime",
                          "datetime-local",
                          "time",
                          "month",
                          "week",
                          "file"])
def test_input_readonly(session, type):
    session.url = inline("<input type=%s readonly>" % type)
    element = session.find.css("input", all=False)

    response = element_clear(session, element)
    assert_error(response, "invalid element state")


def test_textarea(session):
    session.url = inline("<textarea>foobar</textarea>")
    element = session.find.css("textarea", all=False)
    assert element.property("value") == "foobar"

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == ""


def test_textarea_disabled(session):
    session.url = inline("<textarea disabled></textarea>")
    element = session.find.css("textarea", all=False)

    response = element_clear(session, element)
    assert_error(response, "invalid element state")


def test_textarea_readonly(session):
    session.url = inline("<textarea readonly></textarea>")
    element = session.find.css("textarea", all=False)

    response = element_clear(session, element)
    assert_error(response, "invalid element state")


def test_input_file(session, text_file):
    session.url = inline("<input type=file>")
    element = session.find.css("input", all=False)
    element.send_keys(str(text_file))

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == ""


def test_input_file_multiple(session, text_file):
    session.url = inline("<input type=file multiple>")
    element = session.find.css("input", all=False)
    element.send_keys(str(text_file))
    element.send_keys(str(text_file))

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == ""


def test_select(session):
    session.url = inline("""
        <select disabled>
          <option>foo
        </select>
        """)
    select = session.find.css("select", all=False)
    option = session.find.css("option", all=False)

    response = element_clear(session, select)
    assert_error(response, "invalid element state")
    response = element_clear(session, option)
    assert_error(response, "invalid element state")


def test_button(session):
    session.url = inline("<button></button>")
    button = session.find.css("button", all=False)

    response = element_clear(session, button)
    assert_error(response, "invalid element state")


def test_button_with_subtree(session):
    """
    Whilst an <input> is normally editable, the focusable area
    where it is placed will default to the <button>.  I.e. if you
    try to click <input> to focus it, you will hit the <button>.
    """
    session.url = inline("""
        <button>
          <input value=foobar>
        </button>
        """)
    text_field = session.find.css("input", all=False)

    response = element_clear(session, text_field)
    assert_error(response, "element not interactable")


def test_contenteditable(session):
    session.url = inline("<p contenteditable>foobar</p>")
    element = session.find.css("p", all=False)
    assert element.property("innerHTML") == "foobar"

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("innerHTML") == ""


def test_contenteditable_focus(session):
    session.url = inline("""
        <p contenteditable>foobar</p>

        <script>
        window.events = [];
        let p = document.querySelector("p");
        for (let ev of ["focus", "blur"]) {
          p.addEventListener(ev, ({type}) => window.events.push(type));
        }
        </script>
        """)
    element = session.find.css("p", all=False)
    assert element.property("innerHTML") == "foobar"

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("innerHTML") == ""
    assert session.execute_script("return window.events") == ["focus", "blur"]


def test_designmode(session):
    session.url = inline("foobar")
    element = session.find.css("body", all=False)
    assert element.property("innerHTML") == "foobar"
    session.execute_script("document.designMode = 'on'")

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("innerHTML") == "<br>"


def test_resettable_element_focus(session):
    session.url = inline("""
        <input value="foobar">

        <script>
        window.events = [];
        let input = document.querySelector("input");
        for (let ev of ["focus", "blur"]) {
          input.addEventListener(ev, ({type}) => window.events.push(type));
        }
        </script>
        """)
    element = session.find.css("input", all=False)
    assert element.property("value") == "foobar"

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == ""
    assert session.execute_script("return window.events") == ["focus", "blur"]


def test_resettable_element_focus_when_empty(session):
    session.url = inline("""
        <input>

        <script>
        window.events = [];
        let p = document.querySelector("input");
        for (let ev of ["focus", "blur"]) {
          p.addEventListener(ev, ({type}) => window.events.push(type));
        }
        </script>
        """)
    element = session.find.css("input", all=False)
    assert element.property("value") == ""

    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == ""
    assert session.execute_script("return window.events") == []


@pytest.mark.parametrize("type",
                         ["checkbox",
                          "radio",
                          "hidden",
                          "submit",
                          "button",
                          "image"])
def test_non_editable_inputs(session, type):
    session.url = inline("<input type=%s>" % type)
    element = session.find.css("input", all=False)

    response = element_clear(session, element)
    assert_error(response, "invalid element state")


def test_scroll_into_view(session):
    session.url = inline("""
        <input value=foobar>
        <div style='height: 200vh; width: 5000vh'>
        """)
    element = session.find.css("input", all=False)
    assert element.property("value") == "foobar"
    assert session.execute_script("return window.scrollY") == 0

    # scroll to the bottom right of the page
    session.execute_script("""
        let {scrollWidth, scrollHeight} = document.body;
        window.scrollTo(scrollWidth, scrollHeight);
        """)

    # clear and scroll back to the top of the page
    response = element_clear(session, element)
    assert_success(response)
    assert element.property("value") == ""

    # check if element cleared is scrolled into view
    rect = session.execute_script("""
        let [input] = arguments;
        return input.getBoundingClientRect();
        """, args=(element,))
    window = session.execute_script("""
        let {innerHeight, innerWidth, pageXOffset, pageYOffset} = window;
        return {innerHeight, innerWidth, pageXOffset, pageYOffset};
        """)

    assert rect["top"] < (window["innerHeight"] + window["pageYOffset"]) and \
           rect["left"] < (window["innerWidth"] + window["pageXOffset"]) and \
           (rect["top"] + element.rect["height"]) > window["pageYOffset"] and \
           (rect["left"] + element.rect["width"]) > window["pageXOffset"]
