#ifndef AUTOMAPPINGSTATE_H
#define AUTOMAPPINGSTATE_H

#include <QLineF>
#include <QPolygonF>
#include <QVector>

enum class AutoMappingState {
    Inactive,
    SelectingGuideLines,
    GeneratingPolygons,
    RefiningPolygons,
    Finished
};

struct AutoMappingData {
    AutoMappingState state = AutoMappingState::Inactive;
    QVector<QLineF> guideLines;
    QVector<QPolygonF> areaPolygons;
    // Additional fields can be added here
};

#include <QMetaType>
Q_DECLARE_METATYPE(AutoMappingData)

#endif // AUTOMAPPINGSTATE_H

