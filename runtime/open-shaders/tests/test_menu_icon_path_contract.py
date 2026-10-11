from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "runtime/open-shaders"


def test_icon_loader_resolves_logo_from_the_shipped_interface_root():
    loader = (RUNTIME / "src/Menu/IconLoader.cpp").read_text(encoding="utf-8")
    assert 'GetInterfacePath() : Util::PathHelpers::GetIconsPath()' in loader
    assert 'iconDef.filename.starts_with("OpenNR Logo\\\\")' in loader
    package = RUNTIME / "package/Interface/CommunityShaders/OpenNR Logo"
    assert (package / "opennr-logo.png").is_file()
    assert (package / "Monochrome/opennr-logo.png").is_file()
    assert '"OpenNR Logo\\\\opennr-logo.png"' in loader
    assert '"OpenNR Logo\\\\Monochrome\\\\opennr-logo.png"' in loader


def test_menu_icon_list_contains_twenty_two_definitions():
    loader = (RUNTIME / "src/Menu/IconLoader.cpp").read_text(encoding="utf-8")
    definitions = loader.split("return {", 1)[1].split("\n\t\t};", 1)[0]
    assert definitions.count("texture, &menu->uiIcons.") == 22
