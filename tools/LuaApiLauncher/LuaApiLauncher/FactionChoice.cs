using System;
using System.Windows;
using System.Windows.Markup;
using System.Windows.Media;

namespace LuaApiLauncher;

/// <summary>
/// One entry of the faction drop down: the country name and its small icon.
///
/// The combo box gets data items with a template instead of ready made
/// elements. WPF moves UIElement items in and out of the selection box when
/// the drop down opens (it takes the element out of the list and shows it in
/// the closed box), and that moved the label of the closed box to the left
/// after the first click. A data item is always rendered by the same
/// template, so the label stays where it is.
/// </summary>
public sealed class FactionChoice
{
    public FactionChoice(string name, ImageSource? icon, bool isHeader = false, bool isChild = false, FactionOption? faction = null, string? description = null)
    {
        Name = name;
        Icon = icon;
        IsHeader = isHeader;
        IsChild = isChild;
        Faction = faction;
        Description = description;
    }

    public string Name { get; }

    public ImageSource? Icon { get; }

    /// <summary>Group header: rendered bold and cannot be selected.</summary>
    public bool IsHeader { get; }

    /// <summary>A faction inside a group: rendered with a small indent.</summary>
    public bool IsChild { get; }

    /// <summary>The rules faction this item selects, null for Random and headers.</summary>
    public FactionOption? Faction { get; }

    /// <summary>Hover text shown in the faction drop down.</summary>
    public string? Description { get; }

    /// <summary>
    /// The entry of the closed box and of the open list: the icon in the
    /// first column, the name in the second one. A faction without an icon
    /// leaves that column empty, so the name has the whole width of the
    /// field and a long name like "European Union" fits next to the arrow.
    /// A name that still does not fit is cut off with an ellipsis.
    /// </summary>
    public static DataTemplate ItemTemplate { get; } = (DataTemplate)XamlReader.Parse(IconXaml);

    private const string IconXaml = @"
<DataTemplate xmlns=""http://schemas.microsoft.com/winfx/2006/xaml/presentation""
             xmlns:x=""http://schemas.microsoft.com/winfx/2006/xaml"">
  <Grid>
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width=""Auto""/>
      <ColumnDefinition Width=""*""/>
    </Grid.ColumnDefinitions>
    <Image Grid.Column=""0"" Source=""{Binding Icon}"" Width=""21"" Height=""14"" Margin=""0,0,6,0""
           Stretch=""Uniform"" SnapsToDevicePixels=""True"" VerticalAlignment=""Center"">
      <Image.Style>
        <Style TargetType=""Image"">
          <Setter Property=""Visibility"" Value=""Visible""/>
          <Style.Triggers>
            <DataTrigger Binding=""{Binding Icon}"" Value=""{x:Null}"">
              <Setter Property=""Visibility"" Value=""Collapsed""/>
            </DataTrigger>
          </Style.Triggers>
        </Style>
      </Image.Style>
    </Image>
    <TextBlock Grid.Column=""1"" Text=""{Binding Name}"" VerticalAlignment=""Center""
               TextTrimming=""CharacterEllipsis"">
      <TextBlock.Style>
        <Style TargetType=""TextBlock"">
          <Style.Triggers>
            <DataTrigger Binding=""{Binding IsHeader}"" Value=""True"">
              <Setter Property=""FontWeight"" Value=""SemiBold""/>
              <Setter Property=""Foreground"" Value=""#9FC6DD""/>
            </DataTrigger>
            <DataTrigger Binding=""{Binding IsChild}"" Value=""True"">
              <Setter Property=""Margin"" Value=""14,0,0,0""/>
            </DataTrigger>
          </Style.Triggers>
        </Style>
      </TextBlock.Style>
    </TextBlock>
  </Grid>
</DataTemplate>";
}
